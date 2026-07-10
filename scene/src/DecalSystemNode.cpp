#include "scene/DecalSystemNode.hpp"
#include <cmath>

// orthonormal basis in the surface plane (perpendicular to the normal),
// rotated by the decal's own rotation
static void tangentFrame(const Vec3& n, float rot, Vec3& right, Vec3& up)
{
    Vec3 ref = (fabsf(n.y) < 0.99f) ? Vec3(0, 1, 0) : Vec3(1, 0, 0);
    right = Vec3::Cross(ref, n).normalized();
    up = Vec3::Cross(n, right);
    if (rot != 0.f)
    {
        float c = cosf(rot), s = sinf(rot);
        Vec3 r = right, u = up;
        right = r * c - u * s;
        up = r * s + u * c;
    }
}

DecalSystemNode::DecalSystemNode(const std::string& name, int maxDecals)
    : Node3D(name), m_maxDecals(maxDecals)
{
    m_type = NT_DECALSYSTEM;
}

int DecalSystemNode::add(const Vec3& pos, const Vec3& normal, const Vec2& size, const Vec4& color,
                         float lifetime)
{
    Decal d;
    d.position = pos;
    d.normal = normal.normalized();
    d.size = size;
    d.color = color;
    d.lifetime = (lifetime < 0.f) ? m_defLifetime : lifetime;
    d.fadeStart = m_defFade;
    d.active = true;
    // reuse a dead slot if there is one
    for (int i = 0; i < (int)m_decals.size(); ++i)
    {
        if (!m_decals[i].active)
        {
            m_decals[i] = d;
            m_dirty = true;
            return i;
        }
    }
    if ((int)m_decals.size() >= m_maxDecals)
        m_decals[0] = d; // recycle the oldest
    else
        m_decals.push_back(d);
    m_dirty = true;
    return (int)m_decals.size() - 1;
}

int DecalSystemNode::add(const Vec3& pos, const Vec3& normal, float lifetime)
{
    return add(pos, normal, m_defSize, Vec4(1, 1, 1, 1), lifetime);
}

void DecalSystemNode::remove(int idx)
{
    if (idx >= 0 && idx < (int)m_decals.size())
    {
        m_decals[idx].active = false;
        m_dirty = true;
    }
}

void DecalSystemNode::clear()
{
    m_decals.clear();
    m_dirty = true;
}

void DecalSystemNode::_update(float dt)
{
    for (Decal& d : m_decals)
    {
        if (!d.active || d.lifetime <= 0.f) continue;
        d.timeAlive += dt;
        if (d.timeAlive >= d.lifetime) d.active = false;
        m_dirty = true; // alpha fades every frame regardless
    }
    if (m_dirty) rebuild();
}

bool DecalSystemNode::ensure_gpu()
{
    if (m_gpu_ready) return true;
    std::vector<MeshVertex> verts((size_t)m_maxDecals * 4u);
    std::vector<u32> indices((size_t)m_maxDecals * 6u);
    for (int q = 0; q < m_maxDecals; ++q)
    {
        u32 base = (u32)(q * 4);
        u32* idx = &indices[(size_t)q * 6];
        idx[0] = base;
        idx[1] = base + 1;
        idx[2] = base + 2;
        idx[3] = base + 2;
        idx[4] = base + 3;
        idx[5] = base;
    }
    m_mesh.set_data(verts.data(), (u32)verts.size(), indices.data(), (u32)indices.size());
    m_mesh.upload_dynamic();
    m_gpu_ready = true;
    return true;
}

// Same convention as ParticleSystemNode: MeshVertex has no color field, so
// the unused tangent (vec4) carries RGBA — the shared particle/decal quad
// shader reads it as such.
void DecalSystemNode::rebuild()
{
    if (!ensure_gpu()) return;

    static std::vector<MeshVertex> verts;
    verts.assign((size_t)m_maxDecals * 4u, MeshVertex{});
    int q = 0;
    for (const Decal& d : m_decals)
    {
        if (!d.active || q >= m_maxDecals) continue;
        Vec3 right, up;
        tangentFrame(d.normal, d.rotation, right, up);
        float hw = d.size.x * 0.5f, hh = d.size.y * 0.5f;

        float alpha = d.color.w;
        if (d.lifetime > 0.f && d.timeAlive / d.lifetime > d.fadeStart)
        {
            float t = (d.timeAlive / d.lifetime - d.fadeStart) / (1.f - d.fadeStart);
            alpha *= 1.f - Clamp(t, 0.f, 1.f);
        }
        Vec4 col(d.color.x, d.color.y, d.color.z, alpha);

        MeshVertex* v = &verts[(size_t)q * 4];
        v[0].position = d.position - right * hw - up * hh;
        v[0].uv = Vec2(0, 1);
        v[0].tangent = col;
        v[1].position = d.position + right * hw - up * hh;
        v[1].uv = Vec2(1, 1);
        v[1].tangent = col;
        v[2].position = d.position + right * hw + up * hh;
        v[2].uv = Vec2(1, 0);
        v[2].tangent = col;
        v[3].position = d.position - right * hw + up * hh;
        v[3].uv = Vec2(0, 0);
        v[3].tangent = col;
        ++q;
    }
    m_activeQuads = q;
    m_mesh.update_vertices(verts.data(), (u32)verts.size());
    m_dirty = false;
}
