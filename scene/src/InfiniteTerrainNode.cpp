#include "scene/InfiniteTerrainNode.hpp"
#include "scene/Material.hpp"
#include <coregl/gl_log.hpp>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

InfiniteTerrainNode::~InfiniteTerrainNode()
{
    for (auto& kv : m_cache)
        delete kv.second;
    delete[] m_heights;
}

bool InfiniteTerrainNode::load_base_heightmap(const char* path, float heightScale)
{
    scene::Pixmap img;
    if (!img.load(path))
    {
        gl::Log::Error("InfiniteTerrainNode: cannot open heightmap '%s'", path);
        return false;
    }
    m_hmW = img.width;
    m_hmH = img.height;
    m_hmScale = heightScale;
    delete[] m_heights;
    m_heights = new float[(size_t)m_hmW * m_hmH];
    for (u32 z = 0; z < m_hmH; ++z)
        for (u32 x = 0; x < m_hmW; ++x)
            m_heights[(size_t)z * m_hmW + x] = (float)img.get_pixel_color(x, z).r() / 255.f;
    return true;
}

void InfiniteTerrainNode::configure(int visibleHalf, int vertsPerPatch, float patchWorld,
                                    float heightmapWorld)
{
    m_visibleHalf = visibleHalf;
    m_vertsPerPatch = vertsPerPatch;
    m_patchWorld = patchWorld;
    m_hmWorldSize = heightmapWorld;
}

void InfiniteTerrainNode::set_material(Material* m)
{
    m_material = m;
    for (auto& kv : m_cache)
        if (kv.second->node) kv.second->node->set_material(m);
}

float InfiniteTerrainNode::sample_base(float u, float v) const
{
    if (!m_heights) return 0.f;
    u -= std::floor(u);
    v -= std::floor(v);
    float fx = u * (m_hmW - 1), fz = v * (m_hmH - 1);
    int x0 = (int)fx, z0 = (int)fz;
    float tx = fx - x0, tz = fz - z0;
    int x1 = (x0 + 1) % (int)m_hmW, z1 = (z0 + 1) % (int)m_hmH;
    auto H = [&](int x, int z) { return m_heights[(size_t)z * m_hmW + x]; };
    float a = H(x0, z0) + (H(x1, z0) - H(x0, z0)) * tx;
    float b = H(x0, z1) + (H(x1, z1) - H(x0, z1)) * tx;
    return (a + (b - a) * tz) * m_hmScale;
}

Vec3 InfiniteTerrainNode::calc_normal_uv(float u, float v) const
{
    float du = 1.f / (float)(m_hmW - 1), dv = 1.f / (float)(m_hmH - 1);
    float hL = sample_base(u - du, v), hR = sample_base(u + du, v);
    float hD = sample_base(u, v - dv), hU = sample_base(u, v + dv);
    float sx = m_patchWorld * du * (m_hmW - 1);
    float sz = m_patchWorld * dv * (m_hmH - 1);
    return Vec3((hL - hR) / (2.f * sx), 1.f, (hD - hU) / (2.f * sz)).normalized();
}

int InfiniteTerrainNode::calc_lod(float distSq) const
{
    float d0 = m_patchWorld * 2.f;
    for (int i = 0; i < MAX_LOD - 1; ++i)
    {
        float dt = d0 * (float)(1 << i);
        if (distSq < dt * dt) return i;
    }
    return MAX_LOD - 1;
}

void InfiniteTerrainNode::build_mesh(PatchEntry* patch, int px, int pz, int lod)
{
    const int stride = 1 << lod;
    const int verts = m_vertsPerPatch;
    const float wX0 = px * m_patchWorld, wZ0 = pz * m_patchWorld;
    const float frac = 1.f / (float)(verts - 1);
    // world span of one heightmap repetition: wraps UVs into the base map
    const float totalSize = m_hmWorldSize;

    // build vertex index list with LOD stride
    std::vector<int> xi;
    for (int i = 0; i < verts; i += stride)
        xi.push_back(i);
    if (xi.back() != verts - 1) xi.push_back(verts - 1);
    std::vector<int> zi = xi;

    int vcX = (int)xi.size(), vcZ = (int)zi.size();
    std::vector<MeshVertex> vts;
    std::vector<u32> idx;
    BoundingBox aabb;
    bool first = true;

    for (int iz = 0; iz < vcZ; ++iz)
    {
        for (int ix = 0; ix < vcX; ++ix)
        {
            float wx = wX0 + xi[ix] * frac * m_patchWorld;
            float wz = wZ0 + zi[iz] * frac * m_patchWorld;
            float u = wx / totalSize, v = wz / totalSize;
            float h = sample_base(u, v);

            MeshVertex mv;
            mv.position = Vec3(wx, h, wz);
            mv.normal = calc_normal_uv(u, v);
            mv.tangent = Vec4(1.f, 0.f, 0.f, 1.f);
            mv.uv = Vec2(u, v); // base texture covers one heightmap repetition
            if (first)
            {
                aabb.min = aabb.max = mv.position;
                first = false;
            }
            else
            {
                aabb.min = aabb.min.Min(mv.position);
                aabb.max = aabb.max.Max(mv.position);
            }
            vts.push_back(mv);
        }
    }

    for (int iz = 0; iz < vcZ - 1; ++iz)
    {
        for (int ix = 0; ix < vcX - 1; ++ix)
        {
            u32 tl = iz * vcX + ix, tr = tl + 1;
            u32 bl = (iz + 1) * vcX + ix, br = bl + 1;
            idx.push_back(tl);
            idx.push_back(bl);
            idx.push_back(tr);
            idx.push_back(tr);
            idx.push_back(bl);
            idx.push_back(br);
        }
    }

    // ── Skirts: vertical walls around each patch to hide T-junctions ──
    float skirtDepth = (aabb.max.y - aabb.min.y) + m_patchWorld * 0.05f;
    auto addSkirt = [&](const std::vector<u32>& edge)
    {
        u32 base = (u32)vts.size();
        for (u32 gi : edge)
        {
            MeshVertex s = vts[gi];
            s.position.y -= skirtDepth;
            vts.push_back(s);
        }
        for (size_t i = 0; i + 1 < edge.size(); ++i)
        {
            u32 t0 = edge[i], t1 = edge[i + 1];
            u32 b0 = base + (u32)i, b1 = base + (u32)i + 1;
            idx.push_back(t0);
            idx.push_back(b0);
            idx.push_back(t1);
            idx.push_back(t1);
            idx.push_back(b0);
            idx.push_back(b1);
        }
    };
    std::vector<u32> eB, eT, eL, eR;
    for (int ix = 0; ix < vcX; ++ix)
    {
        eB.push_back((u32)ix);
        eT.push_back((u32)((vcZ - 1) * vcX + ix));
    }
    for (int iz = 0; iz < vcZ; ++iz)
    {
        eL.push_back((u32)(iz * vcX));
        eR.push_back((u32)(iz * vcX + vcX - 1));
    }
    addSkirt(eB);
    addSkirt(eT);
    addSkirt(eL);
    addSkirt(eR);
    aabb.min.y -= skirtDepth; // include skirts in the cull bounds

    Mesh& mesh = patch->meshes[lod];
    mesh.set_data(vts.data(), (u32)vts.size(), idx.data(), (u32)idx.size());
    mesh.add_surface(0, (u32)idx.size(), 0, aabb);
    mesh.upload();
    patch->built[lod] = true;
    patch->aabb = aabb;
}

InfiniteTerrainNode::PatchEntry* InfiniteTerrainNode::get_or_create(int px, int pz)
{
    long long key = patch_key(px, pz);
    auto it = m_cache.find(key);
    PatchEntry* p = (it != m_cache.end()) ? it->second : nullptr;
    if (!p)
    {
        p = new PatchEntry();
        m_cache[key] = p;
    }
    p->lastFrame = m_frame;
    return p;
}

void InfiniteTerrainNode::evict_old()
{
    if ((int)m_cache.size() <= MAX_CACHED) return;
    std::vector<std::pair<u32, long long>> items;
    for (auto& kv : m_cache)
        items.push_back({kv.second->lastFrame, kv.first});
    std::sort(items.begin(), items.end());
    int rm = (int)m_cache.size() - MAX_CACHED / 2;
    for (int i = 0; i < rm && i < (int)items.size(); ++i)
    {
        auto it = m_cache.find(items[i].second);
        if (it != m_cache.end())
        {
            PatchEntry* p = it->second;
            // detach the child from the tree before freeing it, and release
            // its GL objects while the context is alive
            if (p->node)
            {
                p->node->remove_from_parent();
                delete p->node;
            }
            for (int l = 0; l < MAX_LOD; ++l)
                if (p->built[l]) p->meshes[l].release_gpu();
            delete p;
            m_cache.erase(it);
        }
    }
}

void InfiniteTerrainNode::_update(float dt)
{
    (void)dt;
    if (!m_heights) return;
    ++m_frame;

    Vec3 camPos = m_camPosition;

    int cPX = (int)std::floor(camPos.x / m_patchWorld);
    int cPZ = (int)std::floor(camPos.z / m_patchWorld);

    for (int pz = cPZ - m_visibleHalf; pz <= cPZ + m_visibleHalf; ++pz)
    {
        for (int px = cPX - m_visibleHalf; px <= cPX + m_visibleHalf; ++px)
        {
            float wpx = px * m_patchWorld + m_patchWorld * 0.5f;
            float wpz = pz * m_patchWorld + m_patchWorld * 0.5f;
            float dx = wpx - camPos.x, dz = wpz - camPos.z;
            int lod = calc_lod(dx * dx + dz * dz);

            PatchEntry* p = get_or_create(px, pz);
            if (!p->built[lod]) build_mesh(p, px, pz, lod);

            // If LOD changed, swap the mesh on the child node
            if (p->activeLod != lod)
            {
                if (!p->node)
                {
                    p->node = create_child<MeshInstance>("patch");
                    p->node->set_material(m_material);
                }
                p->node->set_mesh(&p->meshes[lod]);
                p->activeLod = lod;
            }
            p->node->visible = true;
        }
    }

    // Hide patches outside the visible ring
    for (auto& kv : m_cache)
    {
        PatchEntry* p = kv.second;
        if (p->node && (u32)(m_frame - p->lastFrame) > 1) p->node->visible = false;
    }

    evict_old();
}

void InfiniteTerrainNode::_release_gpu()
{
    for (auto& kv : m_cache)
    {
        for (int i = 0; i < MAX_LOD; ++i)
        {
            if (kv.second->built[i]) kv.second->meshes[i].release_gpu();
        }
    }
}

float InfiniteTerrainNode::height_at(float wx, float wz) const
{
    if (!m_heights) return 0.f;
    return sample_base(wx / m_hmWorldSize, wz / m_hmWorldSize);
}
