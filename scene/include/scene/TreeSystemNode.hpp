#pragma once

#include "scene/Mesh.hpp"
#include "scene/Node3D.hpp"

namespace gl
{
class Texture;
}

// Crossed billboard trees (two quads at right angles through a shared
// vertical axis), built once into a STATIC mesh — same technique as
// GrassSystemNode (wind sway in the vertex stage, alpha-cutout in the
// fragment stage, own dedicated shader in SceneRenderer) but a distinct
// node type so a game can place/light/tune trees independently of grass —
// different density, wind response, and texture atlas.
//
// Texture atlas: the texture can be an NxM grid of different tree sprites
// (e.g. a 4x4 sheet of species/variants) — set_atlas_grid() declares the
// layout, then each addTree()/fillArea() entry picks a cell, so one draw
// call still renders visually varied trees instead of one sprite repeated
// everywhere.
class TreeSystemNode : public Node3D
{
public:
    static constexpr NodeType ClassType = NT_TREESYSTEM;

    explicit TreeSystemNode(const std::string& name = "trees", int maxTrees = 4000);

    bool is_a(NodeType t) const override { return t == NT_TREESYSTEM || Node3D::is_a(t); }

    // atlas layout the texture is divided into; default 1x1 (whole texture,
    // no atlas) — call before addTree()/fillArea() so cell indices land
    // where you expect.
    void set_atlas_grid(int cols, int rows)
    {
        m_atlasCols = cols > 0 ? cols : 1;
        m_atlasRows = rows > 0 ? rows : 1;
    }

    // atlasCol/atlasRow (0-based, clamped to the grid) pick which cell this
    // tree samples; leave at (0,0) with a 1x1 grid for a plain single texture
    void addTree(const Vec3& pos, const Vec2& size, int atlasCol = 0, int atlasRow = 0,
                const Vec4& color = Vec4(1, 1, 1, 1));
    // scatters `count` trees in a world-space rect centered on `center`,
    // each with a random size and a random atlas cell (uniform over the grid)
    void fillArea(const Vec3& center, float width, float depth, int count, float minSize = 4.f,
                 float maxSize = 8.f, unsigned seed = 42);
    void build(); // bakes the tree list into the static mesh — call after adding
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
    struct Tree
    {
        Vec3 pos;
        Vec2 size;
        Vec4 color;
        int atlasCol, atlasRow;
    };
    void addQuad(std::vector<MeshVertex>& v, const Vec3& center, const Vec3& right, const Vec3& up,
                const Vec2& size, const Vec4& color, const Vec2& uvMin, const Vec2& uvMax);

    std::vector<Tree> m_trees;
    Mesh m_mesh;
    int m_maxTrees;
    u32 m_indexCount = 0;
    int m_atlasCols = 1, m_atlasRows = 1;

    Vec3 m_lightDir{-0.5f, -1.f, -0.35f}, m_lightColor{1, 1, 1};
    float m_ambient = 0.4f;
    Vec3 m_windDir{1, 0, 0.3f};
    // trees sway less and slower than grass by default — it's a trunk with
    // a canopy, not a blade
    float m_windStrength = 0.04f, m_windSpeed = 0.8f, m_time = 0.f, m_cutout = 0.5f;
};
