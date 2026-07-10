#pragma once

#include "scene/Node3D.hpp"
#include "scene/Math.hpp"
#include <coregl/gl_types.hpp>
#include <functional>
#include <unordered_map>
#include <vector>

namespace gl
{
class Texture;
}
class Mesh;
class Material;
class MeshInstance;

// Paged terrain: one node = the whole world. Pages (tiles) are created and
// destroyed around the camera following Ogre's Grid2DPageStrategy: cells
// inside `load_radius` are loaded, cells between load and `hold_radius` are
// kept if already loaded (hysteresis — no ping-pong at the border), anything
// beyond is unloaded. At most one page mesh is built per frame so streaming
// never hitches.
//
// Each page is an internal MeshInstance child, so it rides the existing
// pipeline for free: per-page frustum culling, CSM shadows, forward pass.
//
// The game must call set_camera_position() each frame before Scene::update
// (same contract as InfiniteTerrainNode).
class TerrainPagingNode : public Node3D
{
public:
    static constexpr NodeType ClassType = NT_TERRAINPAGING;

    // world height at (x,z) — procedural function or heightmap sampler
    using HeightFn = std::function<float(float x, float z)>;

    explicit TerrainPagingNode(const std::string& name = "terrain_paging");
    ~TerrainPagingNode() override;

    bool is_a(NodeType t) const override
    {
        return t == NT_TERRAINPAGING || Node3D::is_a(t);
    }

    // ── configuration (call before first update) ──
    // vertices per page edge; must be 2^n+1 (129 mobile/web, 257 desktop)
    TerrainPagingNode& set_page_size(int verts);
    // world-space size of one page edge
    TerrainPagingNode& set_cell_size(float worldUnits);
    // Ogre semantics, world units: load inside, keep until beyond hold
    TerrainPagingNode& set_load_radius(float r);
    TerrainPagingNode& set_hold_radius(float r);
    TerrainPagingNode& set_height_function(HeightFn fn);
    // base texture stretches once per page; detail map tiles over it
    TerrainPagingNode& set_texture(gl::Texture* diffuse, gl::Texture* detail = nullptr,
                                   float detailScale = 40.f);
    // tint each page a distinct color — makes streaming visible
    TerrainPagingNode& set_debug_colors(bool on);

    void set_camera_position(const Vec3& pos) { m_camPos = pos; }

    // ── stats ──
    int page_count() const { return (int)m_pages.size(); }
    int pages_built_total() const { return m_builtTotal; }

    void _update(float dt) override;
    void _release_gpu() override;

private:
    struct Page
    {
        Mesh* mesh = nullptr;
        Material* material = nullptr;
        MeshInstance* instance = nullptr;
    };

    static gl::u64 page_key(gl::i32 x, gl::i32 y)
    {
        return ((gl::u64)(gl::u32)x << 32) | (gl::u64)(gl::u32)y;
    }

    void update_paging(); // Ogre Grid2DPageStrategy::notifyCamera logic
    void build_page(gl::i32 cx, gl::i32 cy);
    void destroy_page(Page& p);

    int m_pageSize = 129;     // verts per edge (2^n+1)
    float m_cellSize = 256.f; // world units per page edge
    float m_loadRadius = 500.f;
    float m_holdRadius = 700.f;
    HeightFn m_heightFn;
    gl::Texture* m_diffuse = nullptr;
    gl::Texture* m_detail = nullptr;
    float m_detailScale = 40.f;
    bool m_debugColors = false;

    Vec3 m_camPos = Vec3(0.f, 0.f, 0.f);
    std::unordered_map<gl::u64, Page> m_pages;
    std::vector<std::pair<gl::i32, gl::i32>> m_buildQueue; // reused each frame
    int m_builtTotal = 0;
};
