#pragma once

#include "scene/Mesh.hpp"
#include "scene/Node3D.hpp"

namespace gl
{
class Texture;
}

// Ported from tmp/core/GrassSystem: crossed billboard quads (Single/Cross/
// TriCross) built once into a STATIC mesh — unlike particles/decals, grass
// never changes after build(), so there is no per-frame VBO rebuild. The
// SceneRenderer draws it with a dedicated shader: wind sway in the vertex
// stage (the top of each blade swings, base stays planted) and alpha-cutout
// in the fragment stage (discard below a threshold — no blending, no
// sorting, so overlapping blades never fight each other).
class GrassSystemNode : public Node3D
{
public:
    static constexpr NodeType ClassType = NT_GRASSSYSTEM;
    enum class GrassType
    {
        Single,
        Cross,
        TriCross
    };

    explicit GrassSystemNode(const std::string& name = "grass", int maxClumps = 2000,
                             GrassType type = GrassType::Cross);

    bool is_a(NodeType t) const override { return t == NT_GRASSSYSTEM || Node3D::is_a(t); }

    void addClump(const Vec3& pos, const Vec2& size = Vec2(0.8f, 1.0f),
                  const Vec4& color = Vec4(1, 1, 1, 1));
    // scatters `count` clumps in a world-space rect centered on `center`
    void fillArea(const Vec3& center, float width, float depth, int count, float minSize = 0.6f,
                  float maxSize = 1.2f, unsigned seed = 42);
    void build(); // bakes m_clumps into the static mesh — call after adding
    void clear();

    gl::Texture* texture = nullptr; // non-owning (AssetManager)

    void set_wind(const Vec3& dir, float strength, float speed)
    {
        m_windDir = dir;
        m_windStrength = strength;
        m_windSpeed = speed;
    }
    void set_cutout(float t) { m_cutout = t; }
    // per-patch lighting override; defaults follow the renderer's sun
    void set_light(const Vec3& dir, const Vec3& color, float ambient)
    {
        m_lightDir = dir;
        m_lightColor = color;
        m_ambient = ambient;
    }

    // ── renderer-side (game code never calls these) ──
    Mesh& mesh() { return m_mesh; }
    u32 index_count() const { return m_indexCount; }
    Vec3 wind_dir() const { return m_windDir; }
    float wind_strength() const { return m_windStrength; }
    float wind_speed() const { return m_windSpeed; }
    float cutout() const { return m_cutout; }
    Vec3 light_dir() const { return m_lightDir; }
    Vec3 light_color() const { return m_lightColor; }
    float ambient() const { return m_ambient; }
    float time() const { return m_time; }

protected:
    void _update(float dt) override { m_time += dt; }
    void _release_gpu() override { m_mesh.release_gpu(); }

private:
    struct Clump
    {
        Vec3 pos;
        Vec2 size;
        Vec4 color;
    };
    void addQuad(std::vector<MeshVertex>& v, const Vec3& center, const Vec3& right,
                const Vec3& up, const Vec2& size, const Vec4& color);

    GrassType m_grassType;
    std::vector<Clump> m_clumps;
    Mesh m_mesh;
    int m_maxClumps, m_quadsPerClump;
    u32 m_indexCount = 0;

    Vec3 m_lightDir{-0.5f, -1.f, -0.35f}, m_lightColor{1, 1, 1};
    float m_ambient = 0.4f;
    Vec3 m_windDir{1, 0, 0.3f};
    float m_windStrength = 0.12f, m_windSpeed = 1.8f, m_time = 0.f, m_cutout = 0.5f;
};
