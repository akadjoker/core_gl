#include "scene/TiledTerrainNode.hpp"
#include <coregl/gl_log.hpp>
#include <cmath>
#include <cstring>
#include <vector>

TiledTerrainNode::~TiledTerrainNode()
{
    delete[] m_tileMap;
}

static u8 tileWrapped(u8* tileMap, u32 mapW, u32 mapH, int x, int z, u8 defaultTile)
{
    if (!tileMap) return defaultTile;
    x = ((x % (int)mapW) + (int)mapW) % (int)mapW;
    z = ((z % (int)mapH) + (int)mapH) % (int)mapH;
    return tileMap[(size_t)z * mapW + x];
}

void TiledTerrainNode::load_tilemap(u32 w, u32 h, const u8* data)
{
    delete[] m_tileMap;
    m_mapW = w;
    m_mapH = h;
    m_tileMap = new u8[(size_t)w * h];
    std::memcpy(m_tileMap, data, (size_t)w * h);
    rebuild_patches();
}

void TiledTerrainNode::rebuild_patches()
{
    if (!m_tileMap) return;
    m_terrainMesh.release_gpu();

    const float stepUV = 1.f / (float)m_tilesInSide;
    const float tileWorld = m_patchLen / (float)m_tilesPerPatch;
    const int pX = (int)std::ceil((float)m_mapW / m_tilesPerPatch);
    const int pZ = (int)std::ceil((float)m_mapH / m_tilesPerPatch);

    std::vector<MeshVertex> verts;
    std::vector<u32> indices;
    struct PatchSurf
    {
        u32 first, count;
        BoundingBox aabb;
    };
    std::vector<PatchSurf> patches;

    for (int pz = 0; pz < pZ; ++pz)
    {
        for (int px = 0; px < pX; ++px)
        {
            const int oX = px * m_tilesPerPatch;
            const int oZ = pz * m_tilesPerPatch;
            const float wX = oX * tileWorld;
            const float wZ = oZ * tileWorld;
            const u32 firstIndex = (u32)indices.size();

            BoundingBox aabb(Vec3(wX, -0.01f, wZ), Vec3(wX + m_patchLen, 0.01f, wZ + m_patchLen));

            for (int tz = 0; tz < m_tilesPerPatch; ++tz)
            {
                for (int tx = 0; tx < m_tilesPerPatch; ++tx)
                {
                    u8 tile =
                        tileWrapped(m_tileMap, m_mapW, m_mapH, oX + tx, oZ + tz, m_defaultTile);
                    int atx = tile % m_tilesInSide;
                    int atz = tile / m_tilesInSide;
                    float u0 = atx * stepUV, v0 = atz * stepUV;
                    float u1 = u0 + stepUV, v1 = v0 + stepUV;

                    float x0 = wX + tx * tileWorld, x1 = x0 + tileWorld;
                    float z0 = wZ + tz * tileWorld, z1 = z0 + tileWorld;
                    u32 base = (u32)verts.size();
                    Vec3 n(0, 1, 0);

                    verts.push_back({Vec3(x0, 0, z0), n, Vec4(1, 0, 0, 1), Vec2(u0, v0)});
                    verts.push_back({Vec3(x1, 0, z0), n, Vec4(1, 0, 0, 1), Vec2(u1, v0)});
                    verts.push_back({Vec3(x0, 0, z1), n, Vec4(1, 0, 0, 1), Vec2(u0, v1)});
                    verts.push_back({Vec3(x1, 0, z1), n, Vec4(1, 0, 0, 1), Vec2(u1, v1)});

                    indices.push_back(base);
                    indices.push_back(base + 2);
                    indices.push_back(base + 1);
                    indices.push_back(base + 1);
                    indices.push_back(base + 2);
                    indices.push_back(base + 3);
                }
            }
            PatchSurf p;
            p.first = firstIndex;
            p.count = (u32)indices.size() - firstIndex;
            p.aabb = aabb;
            patches.push_back(p);
        }
    }

    if (patches.empty()) return;

    m_terrainMesh.set_data(verts.data(), (u32)verts.size(), indices.data(), (u32)indices.size());
    for (const auto& p : patches)
        m_terrainMesh.add_surface(p.first, p.count, 0, p.aabb);
    m_terrainMesh.upload();
    set_mesh(&m_terrainMesh);

    gl::Log::Info("TiledTerrainNode: %ux%u tilemap -> %u patches", m_mapW, m_mapH,
                  (u32)patches.size());
}
