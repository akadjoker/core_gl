#include "scene/TerrainPagingNode.hpp"
#include "scene/Mesh.hpp"
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

void TerrainPagingNode::_update(float dt)
{
    (void)dt;
    if (m_heightFn) update_paging();
}

// Ogre Grid2DPageStrategy::notifyCamera, ported: scan the hold-radius box of
// cells around the camera's cell; cells inside the load radius are requested,
// cells between load and hold are kept if present, everything outside the
// hold box is unloaded. Radii are world units converted to cells.
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
    const gl::i32 xmin = (gl::i32)floorf((float)x - holdRadius);
    const gl::i32 xmax = (gl::i32)ceilf((float)x + holdRadius);
    const gl::i32 ymin = (gl::i32)floorf((float)y - holdRadius);
    const gl::i32 ymax = (gl::i32)ceilf((float)y + holdRadius);
    // inner, active load box
    const gl::i32 loadxmin = (gl::i32)floorf((float)x - loadRadius);
    const gl::i32 loadxmax = (gl::i32)ceilf((float)x + loadRadius);
    const gl::i32 loadymin = (gl::i32)floorf((float)y - loadRadius);
    const gl::i32 loadymax = (gl::i32)ceilf((float)y + loadRadius);

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

    if (m_buildQueue.empty()) return;

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

void TerrainPagingNode::build_page(gl::i32 cx, gl::i32 cy)
{
    const int n = m_pageSize;
    const float step = m_cellSize / (float)(n - 1);
    const float ox = (float)cx * m_cellSize; // page origin, node-local
    const float oz = (float)cy * m_cellSize;

    std::vector<MeshVertex> verts((size_t)n * n);
    for (int j = 0; j < n; ++j)
    {
        for (int i = 0; i < n; ++i)
        {
            const float wx = ox + (float)i * step;
            const float wz = oz + (float)j * step;
            MeshVertex& v = verts[(size_t)j * n + i];
            v.position = Vec3((float)i * step, m_heightFn(wx, wz), (float)j * step);
            // central differences on the height source, step-sized
            const float hl = m_heightFn(wx - step, wz), hr = m_heightFn(wx + step, wz);
            const float hd = m_heightFn(wx, wz - step), hu = m_heightFn(wx, wz + step);
            v.normal = Vec3(hl - hr, 2.f * step, hd - hu).normalized();
            v.tangent = Vec4(1.f, 0.f, 0.f, 1.f);
            // base texture stretches once per page; detail map tiles over it
            v.uv = Vec2((float)i / (float)(n - 1), (float)j / (float)(n - 1));
        }
    }

    std::vector<gl::u32> idx;
    idx.reserve((size_t)(n - 1) * (n - 1) * 6);
    for (int j = 0; j < n - 1; ++j)
    {
        for (int i = 0; i < n - 1; ++i)
        {
            gl::u32 a = (gl::u32)(j * n + i);
            gl::u32 b = a + 1;
            gl::u32 c = a + (gl::u32)n;
            gl::u32 d = c + 1;
            idx.push_back(a);
            idx.push_back(c);
            idx.push_back(b);
            idx.push_back(b);
            idx.push_back(c);
            idx.push_back(d);
        }
    }

    Page p;
    p.mesh = new Mesh();
    p.mesh->set_data(verts.data(), (gl::u32)verts.size(), idx.data(), (gl::u32)idx.size());
    p.mesh->compute_bounds();
    p.mesh->upload();

    p.material = new Material();
    p.material->diffuse = m_diffuse;
    p.material->detail = m_detail;
    p.material->detail_scale = m_detailScale;

    p.instance = create_child<MeshInstance>("page");
    p.instance->set_mesh(p.mesh);
    p.instance->set_material(p.material);
    p.instance->set_position(ox, 0.f, oz);

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
