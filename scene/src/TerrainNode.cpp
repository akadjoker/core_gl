#include "scene/TerrainNode.hpp"
#include "scene/Pixmap.hpp"
#include <coregl/gl_log.hpp>
#include <cmath>
#include <cstring>
#include <vector>

TerrainNode::~TerrainNode()
{
    delete[] m_heightData;
}

float TerrainNode::sample_height(int x, int z) const
{
    if (x < 0) x = 0;
    if (z < 0) z = 0;
    if (x >= m_mapW) x = m_mapW - 1;
    if (z >= m_mapH) z = m_mapH - 1;
    return m_heightData[(size_t)z * m_mapW + x] * m_terrainScale.y;
}

Vec3 TerrainNode::calc_normal(int x, int z) const
{
    // central differences over the scaled heightfield
    const float stepX = m_terrainScale.x / (float)(m_mapW - 1);
    const float stepZ = m_terrainScale.z / (float)(m_mapH - 1);
    float hl = sample_height(x - 1, z), hr = sample_height(x + 1, z);
    float hd = sample_height(x, z - 1), hu = sample_height(x, z + 1);
    return Vec3(hl - hr, 2.f * (stepX + stepZ) * 0.5f, hd - hu).normalized();
}

bool TerrainNode::load_heightmap(const char* path, float sx, float sy, float sz, float texU,
                                 float texV)
{
    scene::Pixmap img;
    if (!img.load(path))
    {
        gl::Log::Error("TerrainNode: cannot open heightmap '%s'", path);
        return false;
    }
    m_mapW = img.width;
    m_mapH = img.height;
    m_terrainScale = Vec3(sx, sy, sz);
    delete[] m_heightData;
    m_heightData = new float[(size_t)m_mapW * m_mapH];
    for (int z = 0; z < m_mapH; ++z)
        for (int x = 0; x < m_mapW; ++x)
            m_heightData[(size_t)z * m_mapW + x] =
                (float)img.get_pixel_color((gl::u32)x, (gl::u32)z).r() / 255.f;
    return build_blocks(texU, texV);
}

bool TerrainNode::build(const float* height01, int size, float sx, float sy, float sz, float texU,
                        float texV)
{
    if (!height01 || size < 2) return false;
    m_mapW = m_mapH = size;
    m_terrainScale = Vec3(sx, sy, sz);
    delete[] m_heightData;
    m_heightData = new float[(size_t)size * size];
    std::memcpy(m_heightData, height01, sizeof(float) * (size_t)size * size);
    return build_blocks(texU, texV);
}

bool TerrainNode::build_blocks(float texU, float texV)
{
    const float invW = 1.f / (float)(m_mapW - 1);
    const float invH = 1.f / (float)(m_mapH - 1);
    const int bX = (m_mapW - 1) / (BLOCK_VERTS - 1) > 0 ? (m_mapW - 1) / (BLOCK_VERTS - 1) : 1;
    const int bZ = (m_mapH - 1) / (BLOCK_VERTS - 1) > 0 ? (m_mapH - 1) / (BLOCK_VERTS - 1) : 1;

    std::vector<MeshVertex> verts;
    std::vector<u32> indices;
    struct Block
    {
        u32 first, count;
        BoundingBox aabb;
    };
    std::vector<Block> blocks;

    for (int bz = 0; bz < bZ; ++bz)
    {
        for (int bx = 0; bx < bX; ++bx)
        {
            const int oX = bx * (BLOCK_VERTS - 1), oZ = bz * (BLOCK_VERTS - 1);
            int eX = oX + BLOCK_VERTS, eZ = oZ + BLOCK_VERTS;
            if (eX > m_mapW) eX = m_mapW;
            if (eZ > m_mapH) eZ = m_mapH;
            const int cX = eX - oX, cZ = eZ - oZ;
            if (cX < 2 || cZ < 2) continue;

            const u32 baseVertex = (u32)verts.size();
            const u32 firstIndex = (u32)indices.size();
            Vec3 mn(1e30f, 1e30f, 1e30f), mx(-1e30f, -1e30f, -1e30f);

            for (int lz = 0; lz < cZ; ++lz)
            {
                for (int lx = 0; lx < cX; ++lx)
                {
                    const int wx = oX + lx, wz = oZ + lz;
                    MeshVertex v;
                    v.position = Vec3(wx * invW * m_terrainScale.x, sample_height(wx, wz),
                                      wz * invH * m_terrainScale.z);
                    v.normal = calc_normal(wx, wz);
                    v.tangent = Vec4(1.f, 0.f, 0.f, 1.f);
                    v.uv = Vec2(wx * invW * texU, wz * invH * texV);
                    mn = mn.Min(v.position);
                    mx = mx.Max(v.position);
                    verts.push_back(v);
                }
            }
            for (int lz = 0; lz < cZ - 1; ++lz)
            {
                for (int lx = 0; lx < cX - 1; ++lx)
                {
                    u32 tl = baseVertex + lz * cX + lx, tr = tl + 1;
                    u32 bl = baseVertex + (lz + 1) * cX + lx, br = bl + 1;
                    indices.push_back(tl);
                    indices.push_back(bl);
                    indices.push_back(tr);
                    indices.push_back(tr);
                    indices.push_back(bl);
                    indices.push_back(br);
                }
            }
            Block b;
            b.first = firstIndex;
            b.count = (u32)indices.size() - firstIndex;
            b.aabb = BoundingBox(mn, mx);
            blocks.push_back(b);
        }
    }
    if (blocks.empty()) return false;

    m_terrainMesh.set_data(verts.data(), (u32)verts.size(), indices.data(), (u32)indices.size());
    for (const Block& b : blocks)
        m_terrainMesh.add_surface(b.first, b.count, 0, b.aabb);
    m_terrainMesh.upload();
    set_mesh(&m_terrainMesh);

    gl::Log::Info("TerrainNode: %dx%d heightmap -> %u blocks", m_mapW, m_mapH, (u32)blocks.size());
    return true;
}

float TerrainNode::height_at(float wx, float wz) const
{
    if (!m_heightData || m_mapW < 2) return 0.f;
    float fx = wx / m_terrainScale.x * (m_mapW - 1);
    float fz = wz / m_terrainScale.z * (m_mapH - 1);
    int x0 = (int)floorf(fx), z0 = (int)floorf(fz);
    float tx = fx - x0, tz = fz - z0;
    float h00 = sample_height(x0, z0), h10 = sample_height(x0 + 1, z0);
    float h01 = sample_height(x0, z0 + 1), h11 = sample_height(x0 + 1, z0 + 1);
    float a = h00 + (h10 - h00) * tx, b = h01 + (h11 - h01) * tx;
    return a + (b - a) * tz;
}

Vec3 TerrainNode::normal_at(float wx, float wz) const
{
    if (!m_heightData) return Vec3(0.f, 1.f, 0.f);
    float fx = wx / m_terrainScale.x * (m_mapW - 1);
    float fz = wz / m_terrainScale.z * (m_mapH - 1);
    return calc_normal((int)floorf(fx + 0.5f), (int)floorf(fz + 0.5f));
}

TerrainRaycastResult TerrainNode::raycast(const Ray& ray, float maxDist) const
{
    TerrainRaycastResult r;
    if (!m_heightData) return r;
    // march at ~terrain resolution, then refine the crossing by bisection
    const float sx = m_terrainScale.x / (float)(m_mapW - 1);
    const float sz = m_terrainScale.z / (float)(m_mapH - 1);
    const float step = sx < sz ? sx : sz;
    for (float t = 0.f; t <= maxDist; t += step * 2.f)
    {
        Vec3 p = ray.pointAt(t);
        if (t > 0.f && p.y <= height_at(p.x, p.z))
        {
            float tLo = t - step * 2.f, tHi = t;
            for (int i = 0; i < 8; ++i)
            {
                float tMid = (tLo + tHi) * 0.5f;
                Vec3 pm = ray.pointAt(tMid);
                if (pm.y <= height_at(pm.x, pm.z))
                    tHi = tMid;
                else
                    tLo = tMid;
            }
            float tF = (tLo + tHi) * 0.5f;
            Vec3 hit = ray.pointAt(tF);
            r.hit = true;
            r.position = hit;
            r.normal = normal_at(hit.x, hit.z);
            r.distance = tF;
            return r;
        }
    }
    return r;
}
