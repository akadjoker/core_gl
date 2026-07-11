#include "scene/TerrainPagingNode.hpp"
#include "scene/Material.hpp"
#include "scene/MeshInstance.hpp"
#include <coregl/gl_log.hpp>
#include <algorithm>
#include <cmath>

TerrainPagingNode::TerrainPagingNode(const std::string& name) : Node3D(name)
{
    m_type = NT_TERRAINPAGING;
}

TerrainPagingNode::~TerrainPagingNode()
{
    // instances are children — the Node destructor frees them; we own the rest
    for (auto& kv : m_pages)
    {
        delete kv.second.mesh;
        delete kv.second.material;
    }
}

TerrainPagingNode& TerrainPagingNode::set_page_size(int verts)
{
    // must be 2^n+1 so LOD levels can halve cleanly (Ogre's batch sizes)
    int n = verts - 1;
    if (verts < 17 || (n & (n - 1)) != 0)
    {
        gl::Log::Warn("TerrainPagingNode: page size %d is not 2^n+1, keeping %d", verts,
                      m_pageSize);
        return *this;
    }
    m_pageSize = verts;
    return *this;
}

TerrainPagingNode& TerrainPagingNode::set_cell_size(float worldUnits)
{
    m_cellSize = worldUnits;
    return *this;
}

TerrainPagingNode& TerrainPagingNode::set_load_radius(float r)
{
    m_loadRadius = r;
    return *this;
}

TerrainPagingNode& TerrainPagingNode::set_hold_radius(float r)
{
    m_holdRadius = r;
    return *this;
}

TerrainPagingNode& TerrainPagingNode::set_extent(gl::i32 minX, gl::i32 minY, gl::i32 maxX,
                                                 gl::i32 maxY)
{
    m_bounded = true;
    m_minX = minX;
    m_minY = minY;
    m_maxX = maxX;
    m_maxY = maxY;
    return *this;
}

TerrainPagingNode& TerrainPagingNode::set_infinite()
{
    m_bounded = false;
    return *this;
}

TerrainPagingNode& TerrainPagingNode::set_prewarm(bool on)
{
    m_prewarm = on;
    return *this;
}

TerrainPagingNode& TerrainPagingNode::set_height_function(HeightFn fn)
{
    m_heightFn = std::move(fn);
    return *this;
}

TerrainPagingNode& TerrainPagingNode::set_texture(gl::Texture* diffuse, gl::Texture* detail,
                                                  float detailScale)
{
    m_diffuse = diffuse;
    m_detail = detail;
    m_detailScale = detailScale;
    return *this;
}

TerrainPagingNode& TerrainPagingNode::set_debug_colors(bool on)
{
    m_debugColors = on;
    for (auto& kv : m_pages)
    {
        if (!kv.second.material) continue;
        if (on)
        {
            gl::i32 x = (gl::i32)(kv.first >> 32), y = (gl::i32)(kv.first & 0xffffffffu);
            kv.second.material->base_color =
                Vec3(0.4f + 0.6f * (float)((x * 3 + y * 7) & 3) / 3.f,
                     0.4f + 0.6f * (float)((x * 5 + y * 11) & 3) / 3.f,
                     0.4f + 0.6f * (float)((x * 13 + y * 17) & 3) / 3.f);
        }
        else
            kv.second.material->base_color = Vec3(1.f, 1.f, 1.f);
    }
    return *this;
}

TerrainPagingNode& TerrainPagingNode::set_max_pixel_error(float px)
{
    m_maxPixelError = px > 0.1f ? px : 0.1f;
    m_cFactor = 0.f; // recomputed on next set_lod_camera; keep old until then
    return *this;
}

// Ogre Terrain::preFindVisibleObjects (W. de Boer 2000):
//   A = 1 / tan(fovy * 0.5)
//   T = 2 * maxPixelError / viewportHeight
//   cFactor = A / T
TerrainPagingNode& TerrainPagingNode::set_lod_camera(float fovyDegrees, int viewportHeight)
{
    const float fovy = fovyDegrees * 3.14159265f / 180.f;
    const float A = 1.f / tanf(fovy * 0.5f);
    const float T = 2.f * m_maxPixelError / (float)viewportHeight;
    m_cFactor = A / T;
    return *this;
}

int TerrainPagingNode::lod_levels() const
{
    // halve until 16 quads per edge remain (Ogre min batch 17 verts)
    int levels = 1, quads = m_pageSize - 1;
    while (quads > 16)
    {
        quads >>= 1;
        ++levels;
    }
    return levels;
}

int TerrainPagingNode::lod_of_page(gl::i32 cx, gl::i32 cy) const
{
    auto it = m_pages.find(page_key(cx, cy));
    return it == m_pages.end() ? -1 : it->second.lod;
}

void TerrainPagingNode::_update(float dt)
{
    (void)dt;
    if (!m_heightFn) return;
    update_paging();
    update_lod();
}

// Ogre Grid2DPageStrategy::notifyCamera, ported: scan the hold-radius box of
// cells around the camera's cell; cells inside the load radius are requested,
// cells between load and hold are kept if present, everything outside the
// hold box is unloaded. Radii are world units converted to cells; the boxes
// are clamped to the extent when the world is bounded (Ogre's cell range).
void TerrainPagingNode::update_paging()
{
    // camera → grid cell (grid origin = this node's origin)
    const float gx = m_camPos.x / m_cellSize;
    const float gy = m_camPos.z / m_cellSize;
    const gl::i32 x = (gl::i32)floorf(gx);
    const gl::i32 y = (gl::i32)floorf(gy);

    const float loadRadius = m_loadRadius / m_cellSize;
    const float holdRadius = m_holdRadius / m_cellSize;

    // hold box: round min down, max up (Ogre)
    gl::i32 xmin = (gl::i32)floorf((float)x - holdRadius);
    gl::i32 xmax = (gl::i32)ceilf((float)x + holdRadius);
    gl::i32 ymin = (gl::i32)floorf((float)y - holdRadius);
    gl::i32 ymax = (gl::i32)ceilf((float)y + holdRadius);
    // inner, active load box
    gl::i32 loadxmin = (gl::i32)floorf((float)x - loadRadius);
    gl::i32 loadxmax = (gl::i32)ceilf((float)x + loadRadius);
    gl::i32 loadymin = (gl::i32)floorf((float)y - loadRadius);
    gl::i32 loadymax = (gl::i32)ceilf((float)y + loadRadius);

    if (m_bounded)
    {
        xmin = std::max(xmin, m_minX);
        xmax = std::min(xmax, m_maxX);
        ymin = std::max(ymin, m_minY);
        ymax = std::min(ymax, m_maxY);
        loadxmin = std::max(loadxmin, m_minX);
        loadxmax = std::min(loadxmax, m_maxX);
        loadymin = std::max(loadymin, m_minY);
        loadymax = std::min(loadymax, m_maxY);
    }

    // unload everything outside the hold box
    for (auto it = m_pages.begin(); it != m_pages.end();)
    {
        gl::i32 cx = (gl::i32)(it->first >> 32), cy = (gl::i32)(it->first & 0xffffffffu);
        if (cx < xmin || cx > xmax || cy < ymin || cy > ymax)
        {
            destroy_page(it->second);
            it = m_pages.erase(it);
        }
        else
            ++it;
    }

    // request missing pages in the load box
    m_buildQueue.clear();
    for (gl::i32 cy = loadymin; cy <= loadymax; ++cy)
        for (gl::i32 cx = loadxmin; cx <= loadxmax; ++cx)
            if (m_pages.find(page_key(cx, cy)) == m_pages.end())
                m_buildQueue.push_back({cx, cy});

    if (m_buildQueue.empty())
    {
        m_warmed = true;
        return;
    }

    // first fill: build everything now so the world is whole when the
    // window opens — no pages popping into view
    if (m_prewarm && !m_warmed)
    {
        for (auto& c : m_buildQueue)
            build_page(c.first, c.second);
        m_warmed = true;
        return;
    }
    m_warmed = true;

    // budget: one page build per frame, nearest cell first (no hitches)
    std::pair<gl::i32, gl::i32> best = m_buildQueue[0];
    float bestD = 1e30f;
    for (auto& c : m_buildQueue)
    {
        float dx = (float)c.first + 0.5f - gx, dy = (float)c.second + 0.5f - gy;
        float d = dx * dx + dy * dy;
        if (d < bestD)
        {
            bestD = d;
            best = c;
        }
    }
    build_page(best.first, best.second);
}

// Ogre TerrainQuadTreeNode::calculateCurrentLod: a page renders at LOD l
// while dist < err[l+1] * cFactor — i.e. while the error of dropping one
// level coarser would still be visible; otherwise it keeps coarsening.
// dist deducts half the bounding radius (Ogre's average worst case).
void TerrainPagingNode::update_lod()
{
    if (m_cFactor <= 0.f) return; // set_lod_camera not called: full res

    const int levels = lod_levels();
    for (auto& kv : m_pages)
    {
        Page& p = kv.second;
        const Vec3 d = m_camPos - p.center;
        float dist = sqrtf(d.x * d.x + d.y * d.y + d.z * d.z) - p.radius * 0.5f;
        if (dist < 0.f) dist = 0.f;

        int lod = levels - 1; // last resort: coarsest always renders
        for (int l = 0; l + 1 < levels; ++l)
        {
            if (dist < p.err[(size_t)l + 1] * m_cFactor)
            {
                lod = l;
                break;
            }
        }

        if (lod != p.lod)
        {
            p.lod = lod;
            build_indices(lod);
            p.mesh->update_indices(m_idxScratch.data(), (gl::u32)m_idxScratch.size());
            p.mesh->set_dynamic_index_count((gl::u32)m_idxScratch.size());
        }
    }
}

// grid triangles at stride 2^lod, plus skirts around the perimeter at the
// same stride (skirt verts live after the n*n grid: bottom, top, left,
// right rows of n each). Skirt quads are emitted with both windings so they
// are visible regardless of which side the crack is seen from.
void TerrainPagingNode::build_indices(int lod)
{
    const int n = m_pageSize;
    const int s = 1 << lod;
    const gl::u32 skirtBase = (gl::u32)(n * n);

    m_idxScratch.clear();
    for (int j = 0; j < n - 1; j += s)
    {
        for (int i = 0; i < n - 1; i += s)
        {
            gl::u32 a = (gl::u32)(j * n + i);
            gl::u32 b = a + (gl::u32)s;
            gl::u32 c = a + (gl::u32)(s * n);
            gl::u32 d = c + (gl::u32)s;
            m_idxScratch.push_back(a);
            m_idxScratch.push_back(c);
            m_idxScratch.push_back(b);
            m_idxScratch.push_back(b);
            m_idxScratch.push_back(c);
            m_idxScratch.push_back(d);
        }
    }

    auto skirtQuad = [this](gl::u32 e0, gl::u32 e1, gl::u32 s0, gl::u32 s1)
    {
        m_idxScratch.push_back(e0);
        m_idxScratch.push_back(s0);
        m_idxScratch.push_back(e1);
        m_idxScratch.push_back(e1);
        m_idxScratch.push_back(s0);
        m_idxScratch.push_back(s1);
        // reverse winding — the crack can face either way
        m_idxScratch.push_back(e0);
        m_idxScratch.push_back(e1);
        m_idxScratch.push_back(s0);
        m_idxScratch.push_back(s0);
        m_idxScratch.push_back(e1);
        m_idxScratch.push_back(s1);
    };

    for (int k = 0; k < n - 1; k += s)
    {
        const gl::u32 ks = (gl::u32)k, ke = ks + (gl::u32)s;
        // bottom edge (j = 0), skirt row 0
        skirtQuad(ks, ke, skirtBase + ks, skirtBase + ke);
        // top edge (j = n-1), skirt row 1
        skirtQuad((gl::u32)((n - 1) * n) + ks, (gl::u32)((n - 1) * n) + ke,
                  skirtBase + (gl::u32)n + ks, skirtBase + (gl::u32)n + ke);
        // left edge (i = 0), skirt row 2
        skirtQuad(ks * (gl::u32)n, ke * (gl::u32)n, skirtBase + 2u * (gl::u32)n + ks,
                  skirtBase + 2u * (gl::u32)n + ke);
        // right edge (i = n-1), skirt row 3
        skirtQuad(ks * (gl::u32)n + (gl::u32)(n - 1), ke * (gl::u32)n + (gl::u32)(n - 1),
                  skirtBase + 3u * (gl::u32)n + ks, skirtBase + 3u * (gl::u32)n + ke);
    }
}

void TerrainPagingNode::build_page(gl::i32 cx, gl::i32 cy)
{
    const int n = m_pageSize;
    const float step = m_cellSize / (float)(n - 1);
    const float ox = (float)cx * m_cellSize; // page origin, node-local
    const float oz = (float)cy * m_cellSize;

    m_vtxScratch.resize((size_t)n * n + (size_t)4 * n);
    float hmin = 1e30f, hmax = -1e30f;
    for (int j = 0; j < n; ++j)
    {
        for (int i = 0; i < n; ++i)
        {
            const float wx = ox + (float)i * step;
            const float wz = oz + (float)j * step;
            MeshVertex& v = m_vtxScratch[(size_t)j * n + i];
            const float h = m_heightFn(wx, wz);
            v.position = Vec3((float)i * step, h, (float)j * step);
            // central differences on the height source, step-sized
            const float hl = m_heightFn(wx - step, wz), hr = m_heightFn(wx + step, wz);
            const float hd = m_heightFn(wx, wz - step), hu = m_heightFn(wx, wz + step);
            v.normal = Vec3(hl - hr, 2.f * step, hd - hu).normalized();
            v.tangent = Vec4(1.f, 0.f, 0.f, 1.f);
            // base texture stretches once per page; detail map tiles over it
            v.uv = Vec2((float)i / (float)(n - 1), (float)j / (float)(n - 1));
            if (h < hmin) hmin = h;
            if (h > hmax) hmax = h;
        }
    }

    // per-LOD max height error: |real height - bilinear interp of the
    // coarser grid| over every skipped vertex (Ogre's height deltas)
    const int levels = lod_levels();
    std::vector<float> err((size_t)levels, 0.f);
    for (int l = 1; l < levels; ++l)
    {
        const int s = 1 << l;
        float e = 0.f;
        for (int j = 0; j < n - 1; j += s)
        {
            for (int i = 0; i < n - 1; i += s)
            {
                const float h00 = m_vtxScratch[(size_t)j * n + i].position.y;
                const float h10 = m_vtxScratch[(size_t)j * n + i + s].position.y;
                const float h01 = m_vtxScratch[(size_t)(j + s) * n + i].position.y;
                const float h11 = m_vtxScratch[(size_t)(j + s) * n + i + s].position.y;
                for (int dj = 0; dj <= s; ++dj)
                {
                    const float fy = (float)dj / (float)s;
                    for (int di = 0; di <= s; ++di)
                    {
                        const float fx = (float)di / (float)s;
                        const float interp = (h00 * (1.f - fx) + h10 * fx) * (1.f - fy) +
                                             (h01 * (1.f - fx) + h11 * fx) * fy;
                        const float real =
                            m_vtxScratch[(size_t)(j + dj) * n + i + di].position.y;
                        const float d = fabsf(real - interp);
                        if (d > e) e = d;
                    }
                }
            }
        }
        err[(size_t)l] = e;
    }

    // skirt vertices: perimeter copies dropped below the deepest error the
    // coarsest LOD can produce, so cracks are always covered
    const float skirtDrop = std::max(1.f, err[(size_t)levels - 1] * 1.5f);
    const gl::u32 skirtBase = (gl::u32)(n * n);
    for (int k = 0; k < n; ++k)
    {
        m_vtxScratch[skirtBase + (size_t)k] = m_vtxScratch[(size_t)k];                 // bottom
        m_vtxScratch[skirtBase + (size_t)n + k] = m_vtxScratch[(size_t)(n - 1) * n + k]; // top
        m_vtxScratch[skirtBase + (size_t)2 * n + k] = m_vtxScratch[(size_t)k * n];       // left
        m_vtxScratch[skirtBase + (size_t)3 * n + k] =
            m_vtxScratch[(size_t)k * n + (n - 1)]; // right
    }
    for (int k = 0; k < 4 * n; ++k)
        m_vtxScratch[skirtBase + (size_t)k].position.y -= skirtDrop;

    build_indices(0);

    Page p;
    p.mesh = new Mesh();
    p.mesh->set_data(m_vtxScratch.data(), (gl::u32)m_vtxScratch.size(), m_idxScratch.data(),
                     (gl::u32)m_idxScratch.size());
    p.mesh->compute_bounds();
    p.mesh->upload_dynamic(); // LOD swaps the index buffer at runtime

    p.material = new Material();
    p.material->diffuse = m_diffuse;
    p.material->detail = m_detail;
    p.material->detail_scale = m_detailScale;

    p.instance = create_child<MeshInstance>("page");
    p.instance->set_mesh(p.mesh);
    p.instance->set_material(p.material);
    p.instance->set_position(ox, 0.f, oz);

    p.err = std::move(err);
    p.center = Vec3(ox + m_cellSize * 0.5f, (hmin + hmax) * 0.5f, oz + m_cellSize * 0.5f);
    const float halfXZ = m_cellSize * 0.70710678f; // half diagonal
    const float halfY = (hmax - hmin) * 0.5f;
    p.radius = sqrtf(halfXZ * halfXZ + halfY * halfY);
    p.lod = 0;

    m_pages[page_key(cx, cy)] = p;
    ++m_builtTotal;
    if (m_debugColors) set_debug_colors(true);
}

void TerrainPagingNode::destroy_page(Page& p)
{
    if (p.instance)
    {
        remove_child(p.instance);
        delete p.instance;
    }
    if (p.mesh) p.mesh->release_gpu();
    delete p.mesh;
    delete p.material;
    p = Page{};
}

void TerrainPagingNode::_release_gpu()
{
    for (auto& kv : m_pages)
        if (kv.second.mesh) kv.second.mesh->release_gpu();
}
