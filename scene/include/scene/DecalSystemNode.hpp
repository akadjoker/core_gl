#pragma once

#include "scene/Mesh.hpp"
#include "scene/Node3D.hpp"
#include "scene/ParticleSystemNode.hpp" // ParticleBlendMode
#include <vector>

namespace gl
{
class Texture;
}

// Ported from tmp/core/DecalSystem: quads projected onto a surface (bullet
// marks, blood, scorch), oriented by the hit NORMAL (not billboarded —
// that's the one real difference from ParticleSystemNode) and fading over
// their lifetime. Same buffer strategy as particles: bakes into a Mesh
// dynamic VBO, color riding in the tangent slot, drawn with the identical
// world-space colored-quad shader the SceneRenderer already loads for
// particles (draw_particles/kParticleVS/FS) — a decal system is just
// another source of those quads, so it reuses that exact pass.
class DecalSystemNode : public Node3D
{
public:
    static constexpr NodeType ClassType = NT_DECALSYSTEM;

    struct Decal
    {
        Vec3 position{0, 0, 0};
        Vec3 normal{0, 1, 0};
        Vec2 size{1, 1};
        Vec4 color{1, 1, 1, 1};
        float rotation = 0.f;
        float lifetime = 10.f; // < 0 = infinite
        float timeAlive = 0.f;
        float fadeStart = 0.8f; // starts fading at 80% of its life
        bool active = true;
    };

    explicit DecalSystemNode(const std::string& name = "decals", int maxDecals = 256);
    ~DecalSystemNode() override = default;

    bool is_a(NodeType t) const override { return t == NT_DECALSYSTEM || Node3D::is_a(t); }

    // position/normal are WORLD space (e.g. a raycast hit against a
    // MeshInstance or TerrainNode); returns a slot index for remove().
    int add(const Vec3& pos, const Vec3& normal, const Vec2& size, const Vec4& color,
            float lifetime = -1.f);
    int add(const Vec3& pos, const Vec3& normal, float lifetime = -1.f);
    void remove(int idx);
    void clear();

    int active_count() const { return m_activeQuads; }

    gl::Texture* texture = nullptr; // non-owning (AssetManager)
    ParticleBlendMode blend = ParticleBlendMode::Alpha;
    void set_default_lifetime(float t) { m_defLifetime = t; }
    void set_default_size(const Vec2& s) { m_defSize = s; }

    // ── renderer-side (game code never calls these) ──
    bool ensure_gpu();
    Mesh& quad_mesh() { return m_mesh; }

protected:
    void _update(float dt) override;
    void _release_gpu() override { m_mesh.release_gpu(); }

private:
    void rebuild(); // (re)bakes the dynamic VBO from active decals

    std::vector<Decal> m_decals;
    Mesh m_mesh;
    bool m_gpu_ready = false;
    int m_maxDecals;
    int m_activeQuads = 0;
    bool m_dirty = true;
    float m_defLifetime = 10.f, m_defFade = 0.8f;
    Vec2 m_defSize{1, 1};
};
