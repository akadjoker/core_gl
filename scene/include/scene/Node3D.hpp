#pragma once

#include "scene/Math.hpp"
#include "scene/Node.hpp"

// Node com transform (modelo Unity: sem Node2D separado). Transform = posição +
// rotação (quaternião) + escala, com matriz local/world em cache por dirty-flag.
// A autoria 2D resolve-se com os helpers *2d (z preservado, rotação em Z).
class Node3D : public Node
{
public:
    static constexpr NodeType ClassType = NT_NODE3D;

    explicit Node3D(const std::string& name = "Node3D");

    bool is_a(NodeType t) const override { return t == NT_NODE3D || Node::is_a(t); }

    // ───────────── Transform LOCAL (relativo ao pai) ─────────────
    // É aqui que o transform é guardado. set_*/get_* sem "global" são locais.
    void set_position(const Vec3& p);
    void set_position(float x, float y, float z);
    const Vec3& get_position() const { return m_position; }

    void set_rotation(const Quaternion& q);
    void set_euler(const Vec3& radians); // ordem YXZ
    const Quaternion& get_rotation() const { return m_rotation; }
    Vec3 get_euler() const { return m_rotation.getEuler(); }

    void set_scale(const Vec3& s);
    void set_scale(float s);
    const Vec3& get_scale() const { return m_scale; }

    // ───────────── Transform GLOBAL (mundo) ─────────────
    // Absoluto, após aplicar toda a cadeia de pais. Os setters convertem de
    // volta para local. (cf. GetWorld*/OverrideWorld* do cNode da AGK.)
    Vec3 get_global_position();
    void set_global_position(const Vec3& p);
    Quaternion get_global_rotation();
    void set_global_rotation(const Quaternion& q);
    const Mat4& get_global_transform() { return get_world_matrix(); }

    // ───────────── Movimento (estilo Blitz3D / AGK) ─────────────
    void move(const Vec3& local_delta); // ao longo da ORIENTAÇÃO (Blitz MoveEntity / MoveLocal)
    void move(float x, float y, float z);
    void move_global(const Vec3& world_delta); // eixos do MUNDO (Blitz TranslateEntity)
    void advance(float distance);              // move(0,0,-d): para a frente (forward -Z)
    void strafe(float distance);               // move(d,0,0): para a direita (+X)
    void lift(float distance);                 // move(0,d,0): para cima (+Y)

    // ───────────── Rotação ─────────────
    void rotate(const Vec3& euler_radians);            // local, YXZ (RotateLocal)
    void rotate_axis(const Vec3& axis, float radians); // local, em torno de eixo
    void rotate_global(const Quaternion& q);           // mundo (RotateGlobal)
    void look_at(const Vec3& target, const Vec3& up = Vec3(0.0f, 1.0f, 0.0f)); // global

    // ───────────── Eixos ─────────────
    Vec3 forward() const;  // -Z local
    Vec3 right() const;    // +X local
    Vec3 up() const;       // +Y local
    Vec3 global_forward(); // -Z mundo
    Vec3 global_right();   // +X mundo
    Vec3 global_up();      // +Y mundo

    // ───────────── Helpers 2D (z preservado; rotação em Z; z_index = z) ─────────────
    void set_position(float x, float y);
    Vec2 get_position2d() const { return Vec2(m_position.x, m_position.y); }
    void set_rotation2d(float radians);
    float get_rotation2d() const { return m_rotation.getEuler().z; }
    void set_z_index(float z);
    float get_z_index() const { return m_position.z; }

    // ───────────── Matrizes (reconstruídas sob procura) ─────────────
    const Mat4& get_local_matrix();
    const Mat4& get_world_matrix();

protected:
    void on_parent_transform_changed() override;

private:
    void mark_dirty();
    void refresh_world();
    Node3D* parent_node3d() const;

    Vec3 m_position;
    Quaternion m_rotation;
    Vec3 m_scale;

    Mat4 m_local;
    Mat4 m_world;
    Quaternion m_world_rotation; // rotação de mundo em cache (para global_*/look_at O(1))
    bool m_local_dirty;
    bool m_world_dirty;
};
