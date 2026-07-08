#include "scene/Node3D.hpp"

// rotates a vector by a quaternion (Math.hpp has no quat*vec operator; the
// quat-built Mat4 has zero translation, so its operator*(Vec3) is a pure
// rotation)
static inline Vec3 rotateVec(const Quaternion& q, const Vec3& v)
{
    return Mat4(q) * v;
}

Node3D::Node3D(const std::string& name)
    : Node(name), m_position(0.f, 0.f, 0.f), m_scale(1.f, 1.f, 1.f), m_local_dirty(true),
      m_world_dirty(true)
{
    m_type = NT_NODE3D;
}

// ───────────── local transform ─────────────

void Node3D::set_position(const Vec3& p)
{
    m_position = p;
    mark_dirty();
}

void Node3D::set_position(float x, float y, float z)
{
    set_position(Vec3(x, y, z));
}

void Node3D::set_rotation(const Quaternion& q)
{
    m_rotation = Quaternion::Normalize(q);
    mark_dirty();
}

void Node3D::set_euler(const Vec3& radians)
{
    set_rotation(Quaternion(radians.x, radians.y, radians.z));
}

void Node3D::set_scale(const Vec3& s)
{
    m_scale = s;
    mark_dirty();
}

void Node3D::set_scale(float s)
{
    set_scale(Vec3(s, s, s));
}

// ───────────── global transform ─────────────

Vec3 Node3D::get_global_position()
{
    const Mat4& w = get_world_matrix();
    return Vec3(w.c[3][0], w.c[3][1], w.c[3][2]);
}

void Node3D::set_global_position(const Vec3& p)
{
    Node3D* parent = parent_node3d();
    if (parent)
    {
        Mat4 invParent = Mat4::Inverse(parent->get_world_matrix());
        set_position(invParent * p);
    }
    else
    {
        set_position(p);
    }
}

Quaternion Node3D::get_global_rotation()
{
    refresh_world();
    return m_world_rotation;
}

void Node3D::set_global_rotation(const Quaternion& q)
{
    Node3D* parent = parent_node3d();
    if (parent)
        set_rotation(parent->get_global_rotation().inverted() * q);
    else
        set_rotation(q);
}

// ───────────── movement ─────────────

void Node3D::move(const Vec3& local_delta)
{
    m_position += rotateVec(m_rotation, local_delta);
    mark_dirty();
}

void Node3D::move(float x, float y, float z)
{
    move(Vec3(x, y, z));
}

void Node3D::move_global(const Vec3& world_delta)
{
    Node3D* parent = parent_node3d();
    if (parent)
        m_position += rotateVec(parent->get_global_rotation().inverted(), world_delta);
    else
        m_position += world_delta;
    mark_dirty();
}

void Node3D::advance(float distance)
{
    move(Vec3(0.f, 0.f, -distance));
}

void Node3D::strafe(float distance)
{
    move(Vec3(distance, 0.f, 0.f));
}

void Node3D::lift(float distance)
{
    move(Vec3(0.f, distance, 0.f));
}

// ───────────── rotation ─────────────

void Node3D::rotate(const Vec3& euler_radians)
{
    set_rotation(m_rotation * Quaternion(euler_radians.x, euler_radians.y, euler_radians.z));
}

void Node3D::rotate_axis(const Vec3& axis, float radians)
{
    set_rotation(m_rotation * Quaternion::FromAxisAngle(axis, radians));
}

void Node3D::rotate_global(const Quaternion& q)
{
    set_global_rotation(Quaternion::Normalize(q) * get_global_rotation());
}

void Node3D::look_at(const Vec3& target, const Vec3& up)
{
    Vec3 dir = target - get_global_position();
    if (dir.length() < 1e-6f) return;
    // LookRotation maps local +Z onto the given direction; our forward is
    // -Z, so hand it the opposite vector
    set_global_rotation(Quaternion::LookRotation(dir * -1.f, up));
}

// ───────────── axes ─────────────

Vec3 Node3D::forward() const
{
    return rotateVec(m_rotation, Vec3(0.f, 0.f, -1.f));
}

Vec3 Node3D::right() const
{
    return rotateVec(m_rotation, Vec3(1.f, 0.f, 0.f));
}

Vec3 Node3D::up() const
{
    return rotateVec(m_rotation, Vec3(0.f, 1.f, 0.f));
}

Vec3 Node3D::global_forward()
{
    return rotateVec(get_global_rotation(), Vec3(0.f, 0.f, -1.f));
}

Vec3 Node3D::global_right()
{
    return rotateVec(get_global_rotation(), Vec3(1.f, 0.f, 0.f));
}

Vec3 Node3D::global_up()
{
    return rotateVec(get_global_rotation(), Vec3(0.f, 1.f, 0.f));
}

// ───────────── 2D helpers ─────────────

void Node3D::set_position(float x, float y)
{
    set_position(Vec3(x, y, m_position.z));
}

void Node3D::set_rotation2d(float radians)
{
    set_rotation(Quaternion(0.f, 0.f, radians));
}

void Node3D::set_z_index(float z)
{
    m_position.z = z;
    mark_dirty();
}

// ───────────── matrices ─────────────

const Mat4& Node3D::get_local_matrix()
{
    if (m_local_dirty)
    {
        m_local = Mat4::Translate(m_position) * Mat4(m_rotation) *
                  Mat4::Scale(m_scale.x, m_scale.y, m_scale.z);
        m_local_dirty = false;
    }
    return m_local;
}

const Mat4& Node3D::get_world_matrix()
{
    refresh_world();
    return m_world;
}

void Node3D::on_parent_transform_changed()
{
    m_world_dirty = true;
    Node::on_parent_transform_changed(); // keep propagating down the tree
}

void Node3D::mark_dirty()
{
    m_local_dirty = true;
    m_world_dirty = true;
    // descendants' world transforms depend on ours
    for (Node* child : m_children)
        child->on_parent_transform_changed();
}

void Node3D::refresh_world()
{
    if (!m_world_dirty && !m_local_dirty) return;

    Node3D* parent = parent_node3d();
    if (parent)
    {
        m_world = parent->get_world_matrix() * get_local_matrix();
        m_world_rotation = Quaternion::Normalize(parent->get_global_rotation() * m_rotation);
    }
    else
    {
        m_world = get_local_matrix();
        m_world_rotation = m_rotation;
    }
    m_world_dirty = false;
}

Node3D* Node3D::parent_node3d() const
{
    // transform chains skip non-3D nodes (e.g. a plain grouping Node)
    for (Node* p = m_parent; p; p = p->get_parent())
    {
        Node3D* p3d = p->as<Node3D>();
        if (p3d) return p3d;
    }
    return nullptr;
}
