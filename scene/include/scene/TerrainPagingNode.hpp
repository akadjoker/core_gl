#pragma once

#include "scene/Node3D.hpp"
#include "scene/Math.hpp"
#include "scene/Mesh.hpp" // MeshVertex
#include <coregl/gl_types.hpp>
#include <functional>
#include <unordered_map>
#include <vector>

namespace gl
{
class Texture;
class Shader;
}
class Mesh;
class Material;
class MeshInstance;

// Paged terrain: one node = the whole world, bounded or infinite. Ported
// from the Ogre sources in tmp/Paging and tmp/Terrain:
//
// - Streaming is Grid2DPageStrategy::notifyCamera: cells inside
//   `load_radius` are loaded, cells between load and `hold_radius` are kept
//   if already loaded (hysteresis), anything beyond is unloaded. At most one
//   page mesh is built per frame after the initial prewarm.
// - LOD is Terrain::preFindVisibleObjects (W. de Boer 2000): each page
//   precomputes, per LOD level, the max height error that level introduces;
//   a page renders at the finest level whose next-coarser error would exceed
//   `max_pixel_error` on screen (distTransition = maxHeightDelta * cFactor).
// - Cracks between pages at different LODs are hidden by skirts.
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
    // bounded world: pages only exist for minX..maxX, minY..maxY (cells,
    // inclusive) — Ogre's cell range. Without it the world is infinite.
    TerrainPagingNode& set_extent(gl::i32 minX, gl::i32 minY, gl::i32 maxX, gl::i32 maxY);
    TerrainPagingNode& set_infinite(); // remove the extent (default)
    // build every page inside the load radius before the first frame is
    // seen, instead of one per frame (no pop-in at startup). Default on.
    TerrainPagingNode& set_prewarm(bool on);
    TerrainPagingNode& set_height_function(HeightFn fn);
    // base texture stretches once per page; detail map tiles over it
    TerrainPagingNode& set_texture(gl::Texture* diffuse, gl::Texture* detail = nullptr,
                                   float detailScale = 40.f);
    // tint each page a distinct color — makes streaming/LOD visible
    TerrainPagingNode& set_debug_colors(bool on);

    // ── texture splatting (Ogre-style) ──
    // layer 0 is the base; layers 1..4 are mixed on top by an RGBA blend
    // map generated per page from the blend function. worldSize = world
    // units one repeat of the texture covers. With layers set, pages are
    // drawn by the renderer's splat pass instead of the forward pass.
    TerrainPagingNode& set_layer(int i, gl::Texture* diffuse, float worldSize);
    // weights for layers 1..4 at a world position; slope: 0 flat, 1 vertical
    using BlendFn = std::function<void(float x, float z, float h, float slope, float w[4])>;
    TerrainPagingNode& set_blend_function(BlendFn fn);
    int layer_count() const { return m_layerCount; }
    gl::Texture* layer_texture(int i) const { return m_layers[i].diffuse; }
    float layer_tiling(int i) const { return m_cellSize / m_layers[i].worldSize; }

    // ── queries ──
    // effective height: height function + accumulated brush edits
    float height_at(float x, float z) const;
    // surface normal of the effective heightfield (grid-step differences)
    Vec3 normal_at(float x, float z) const;
    // ray vs. the effective heightfield: coarse march + bisection refine.
    // Pair with Camera3D::screen_to_ray for mouse picking.
    bool pick(const Vec3& origin, const Vec3& dir, float maxDist, Vec3& hit) const;

    // ── editing (edits persist across page unload/reload) ──
    // smooth circular brush, raises (amount > 0) or lowers
    void modify_height(float x, float z, float radius, float amount);
    // relaxes the area toward its local average (3x3 kernel, falloff rim)
    void smooth_height(float x, float z, float radius, int iterations = 2);

    // renderer-facing: draws every resident page with the bound splat
    // shader (frustum-culled); the renderer owns shader/layer binding
    void render_pages(gl::Shader* shader, gl::i32 locModel, const Frustum* frustum);

    // ── LOD (Ogre defaults) ──
    // max on-screen error in pixels before a page drops to a finer level
    TerrainPagingNode& set_max_pixel_error(float px);
    // the LOD formula needs the camera: vertical fov (degrees) and the
    // viewport height in pixels. Call once, and again on resize.
    TerrainPagingNode& set_lod_camera(float fovyDegrees, int viewportHeight);

    void set_camera_position(const Vec3& pos) { m_camPos = pos; }

    // ── stats ──
    int page_count() const { return (int)m_pages.size(); }
    int pages_built_total() const { return m_builtTotal; }
    int lod_of_page(gl::i32 cx, gl::i32 cy) const; // -1 if not resident

    void _update(float dt) override;
    void _release_gpu() override;

private:
    struct Page
    {
        Mesh* mesh = nullptr;
        Material* material = nullptr;
        MeshInstance* instance = nullptr;
        gl::Texture* blend = nullptr; // RGBA weights for layers 1..4 (splat mode)
        // err[l] = max height error introduced by LOD level l (err[0]=0);
        // Ogre: a page renders at level l while dist < err[l+1]*cFactor
        std::vector<float> err;
        Vec3 center;       // page centre, node-local (for LOD distance)
        float radius = 0.f; // bounding radius (Ogre deducts half of it)
        int lod = 0;
    };

    static gl::u64 page_key(gl::i32 x, gl::i32 y)
    {
        return ((gl::u64)(gl::u32)x << 32) | (gl::u64)(gl::u32)y;
    }

    void update_paging(); 
    void update_lod();    
    void build_page(gl::i32 cx, gl::i32 cy);
    void destroy_page(Page& p);
    void build_indices(int lod);              // fills m_idxScratch (grid + skirts)
    void fill_vertices(gl::i32 cx, gl::i32 cy, float& hmin, float& hmax); // m_vtxScratch
    void update_blend_map(Page& p, gl::i32 cx, gl::i32 cy); // from m_vtxScratch
    void rebuild_page_geometry(Page& p, gl::i32 cx, gl::i32 cy); // after edits
    int lod_levels() const;

    int m_pageSize = 129;     // verts per edge (2^n+1)
    float m_cellSize = 256.f; // world units per page edge
    float m_loadRadius = 500.f;
    float m_holdRadius = 700.f;
    bool m_bounded = false;
    gl::i32 m_minX = 0, m_minY = 0, m_maxX = 0, m_maxY = 0;
    bool m_prewarm = true;
    bool m_warmed = false;
    HeightFn m_heightFn;
    gl::Texture* m_diffuse = nullptr;
    gl::Texture* m_detail = nullptr;
    float m_detailScale = 40.f;
    bool m_debugColors = false;

    float m_maxPixelError = 3.f; 
    float m_cFactor = 0.f;        

    struct Layer
    {
        gl::Texture* diffuse = nullptr;
        float worldSize = 30.f;
    };
    Layer m_layers[5];
    int m_layerCount = 0;
    BlendFn m_blendFn;
    // brush edits, keyed by page: n*n height deltas added on top of the
    // height function — kept when the page unloads so edits survive paging
    std::unordered_map<gl::u64, std::vector<float>> m_edits;
    std::vector<gl::u8> m_blendScratch; // reused per blend-map build

    Vec3 m_camPos = Vec3(0.f, 0.f, 0.f);
    std::unordered_map<gl::u64, Page> m_pages;
    std::vector<std::pair<gl::i32, gl::i32>> m_buildQueue; // reused each frame
    std::vector<gl::u32> m_idxScratch;                     // reused per rebuild
    std::vector<MeshVertex> m_vtxScratch;                  // reused per build
    int m_builtTotal = 0;
};
