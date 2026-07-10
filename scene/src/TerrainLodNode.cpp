#include "scene/TerrainLodNode.hpp"
#include "scene/Material.hpp"
#include "scene/Pixmap.hpp"
#include <coregl/gl_log.hpp>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

TerrainLodNode::~TerrainLodNode()
{
    delete[] m_heightData;
}

// ── helpers ──

void TerrainLodNode::bb_expand(BoundingBox& bb, const Vec3& p, bool& first)
{
    if (first)
    {
        bb.min = bb.max = p;
        first = false;
    }
    else
    {
        bb.min = bb.min.Min(p);
        bb.max = bb.max.Max(p);
    }
}

float TerrainLodNode::sample_height(int x, int z) const
{
    x = std::max(0, std::min(x, m_size - 1));
    z = std::max(0, std::min(z, m_size - 1));
    return m_heightData[z * m_size + x];
}

// ── build ──

void TerrainLodNode::build(const std::vector<float>& height01, int size, float heightScale)
{
    m_size = size;
    m_heightScale = heightScale;

    // clamp maxLOD to patchSize
    switch (m_patchSize)
    {
        case 9:
            m_maxLOD = std::min(m_maxLOD, 3);
            break;
        case 17:
            m_maxLOD = std::min(m_maxLOD, 4);
            break;
        case 33:
            m_maxLOD = std::min(m_maxLOD, 5);
            break;
        case 65:
            m_maxLOD = std::min(m_maxLOD, 6);
            break;
        default:
            m_maxLOD = std::min(m_maxLOD, 7);
            break;
    }

    delete[] m_heightData;
    m_heightData = new float[(size_t)size * size];
    std::memcpy(m_heightData, height01.data(), sizeof(float) * (size_t)size * size);

    rebuild_vertices();
    apply_transformation();
    m_forceRecalc = true;

    gl::Log::Info("TerrainLodNode: size=%d patches=%dx%d maxLOD=%d", m_size, m_patchCount,
                  m_patchCount, m_maxLOD);
}

bool TerrainLodNode::load_heightmap(const char* path, float heightScale, int smoothFactor)
{
    scene::Pixmap img;
    if (!img.load(path))
    {
        gl::Log::Error("TerrainLodNode: cannot open heightmap '%s'", path);
        return false;
    }
    int size = img.width;
    std::vector<float> h((size_t)size * size);
    for (int z = 0; z < size; ++z)
        for (int x = 0; x < size; ++x)
            h[z * size + x] = (float)img.get_pixel_color(x, z).r() / 255.f;

    if (smoothFactor > 0)
    {
        for (int it = 0; it < smoothFactor; ++it)
        {
            for (int z = 1; z < size - 1; ++z)
                for (int x = 1; x < size - 1; ++x)
                    h[z * size + x] = (h[z * size + x - 1] + h[z * size + x + 1] +
                                       h[(z - 1) * size + x] + h[(z + 1) * size + x]) *
                                      0.25f;
        }
    }
    build(h, size, heightScale);
    return true;
}

void TerrainLodNode::rebuild_vertices()
{
    if (m_size < 2) return;
    const float td = 1.f / (float)(m_size - 1);
    m_sourceVerts.resize((size_t)m_size * m_size);
    for (int z = 0; z < m_size; ++z)
    {
        for (int x = 0; x < m_size; ++x)
        {
            float fx = x * td, fz = z * td;
            MeshVertex& v = m_sourceVerts[(size_t)z * m_size + x];
            v.position = Vec3(fx, m_heightData[z * m_size + x] * m_heightScale, fz);
            v.normal = Vec3(0, 1, 0);
            v.tangent = Vec4(1, 0, 0, 1);
            v.uv = Vec2(fx * m_texScale, fz * m_texScale);
        }
    }
}

void TerrainLodNode::calculate_normals()
{
    auto P = [&](int x, int z) -> Vec3&
    {
        x = std::max(0, std::min(x, m_size - 1));
        z = std::max(0, std::min(z, m_size - 1));
        return m_verts[(size_t)z * m_size + x].position;
    };
    for (int z = 0; z < m_size; ++z)
    {
        for (int x = 0; x < m_size; ++x)
        {
            Vec3 n = Vec3::Cross(P(x, z + 1) - P(x, z - 1), P(x + 1, z) - P(x - 1, z));
            float l = n.length();
            m_verts[(size_t)z * m_size + x].normal = (l > 1e-6f) ? n * (1.f / l) : Vec3(0, 1, 0);
        }
    }
}

void TerrainLodNode::bake_positions()
{
    m_verts.resize(m_sourceVerts.size());
    for (size_t i = 0; i < m_sourceVerts.size(); ++i)
    {
        const MeshVertex& src = m_sourceVerts[i];
        MeshVertex& d = m_verts[i];
        d.position =
            Vec3(src.position.x * m_scale.x + m_pos.x, src.position.y * m_scale.y + m_pos.y,
                 src.position.z * m_scale.z + m_pos.z);
        d.normal = src.normal;
        d.tangent = src.tangent;
        d.uv = src.uv;
    }
}

void TerrainLodNode::apply_transformation()
{
    if (m_sourceVerts.empty()) return;
    bake_positions();
    calculate_normals();
    calculate_distance_thresholds();
    create_patches();
    calculate_patch_data();

    // Upload mesh with dynamic buffers (IBO will be updated per-frame)
    m_terrainMesh.release_gpu();
    m_terrainMesh.set_data(m_verts.data(), (u32)m_verts.size(), m_indices.data(),
                           m_indices.empty() ? 1 : (u32)m_indices.size());
    m_terrainMesh.upload_dynamic();
    set_mesh(&m_terrainMesh);
}

void TerrainLodNode::calculate_distance_thresholds()
{
    m_lodDist.resize(m_maxLOD);
    float normPatch = (float)m_calcPatchSize / (float)(m_size - 1);
    float px = normPatch * m_scale.x, pz = normPatch * m_scale.z;
    float diag = sqrtf(px * px + pz * pz);
    for (int i = 0; i < m_maxLOD; ++i)
    {
        float dd = diag * powf(2.f, (float)i);
        m_lodDist[i] = dd * dd;
    }
}

void TerrainLodNode::create_patches()
{
    m_patchCount = (m_size - 1) / m_calcPatchSize;
    m_patches.assign((size_t)m_patchCount * m_patchCount, Patch{});

    // Pre-allocate max possible indices
    m_maxIndices = (u32)m_patchCount * m_patchCount * m_calcPatchSize * m_calcPatchSize * 6u;
    m_indices.assign(m_maxIndices, 0u);
}

void TerrainLodNode::calculate_patch_data()
{
    if (m_patches.empty()) return;
    bool firstAll = true;
    for (int px = 0; px < m_patchCount; ++px)
    {
        for (int pz = 0; pz < m_patchCount; ++pz)
        {
            Patch& p = m_patches[(size_t)px * m_patchCount + pz];
            int xS = pz * m_calcPatchSize, zS = px * m_calcPatchSize;
            int xE = xS + m_calcPatchSize, zE = zS + m_calcPatchSize;
            bool first = true;
            for (int x = xS; x <= xE; ++x)
                for (int z = zS; z <= zE; ++z)
                    bb_expand(p.aabb, m_verts[(size_t)z * m_size + x].position, first);
            p.center = p.aabb.center();
            p.top = (px > 0) ? &m_patches[(size_t)(px - 1) * m_patchCount + pz] : nullptr;
            p.bottom = (px < m_patchCount - 1) ? &m_patches[(size_t)(px + 1) * m_patchCount + pz]
                                               : nullptr;
            p.left = (pz > 0) ? &m_patches[(size_t)px * m_patchCount + (pz - 1)] : nullptr;
            p.right = (pz < m_patchCount - 1) ? &m_patches[(size_t)px * m_patchCount + (pz + 1)]
                                              : nullptr;

            if (firstAll)
            {
                m_aabb = p.aabb;
                firstAll = false;
            }
            else
            {
                m_aabb.Merge(p.aabb);
            }
        }
    }
}

// ── LOD selection ──

bool TerrainLodNode::pre_render_lod()
{
    if (!m_forceRecalc)
    {
        Vec3 dp = m_camPos - m_oldCamPos;
        if (dp.length_squared() < m_camMoveDelta * m_camMoveDelta &&
            Vec3::Dot(m_camFwd, m_oldCamFwd) > m_camRotDelta)
            return false;
    }
    m_oldCamPos = m_camPos;
    m_oldCamFwd = m_camFwd;
    m_forceRecalc = false;

    // Build frustum from an identity-ish transform — we just need AABB testing
    // against the camera. Since we don't have the projection here, we skip
    // frustum culling and rely on distance-based LOD only. The per-surface
    // frustum cull in Scene::collect handles visibility.

    int count = m_patchCount * m_patchCount;
    for (int j = 0; j < count; ++j)
    {
        Patch& p = m_patches[j];
        // Simple distance-based LOD (no frustum culling here)
        Vec3 dc = m_camPos - p.center;
        float distSq = Vec3::Dot(dc, dc);
        p.currentLOD = 0;
        for (int i = m_maxLOD - 1; i > 0; --i)
        {
            if (distSq >= m_lodDist[i])
            {
                p.currentLOD = i;
                break;
            }
        }
    }
    return true;
}

u32 TerrainLodNode::get_index(int patchX, int patchZ, int patchIdx, u32 vX, u32 vZ) const
{
    const Patch& p = m_patches[patchIdx];
    const u32 cs = (u32)m_calcPatchSize;

    // T-junction stitching: snap edge vertices to the coarser neighbor's step
    if (vZ == 0 && p.top && p.top->currentLOD >= 0 && p.currentLOD < p.top->currentLOD &&
        (vX % (1u << p.top->currentLOD)) != 0)
        vX -= vX % (1u << p.top->currentLOD);
    else if (vZ == cs && p.bottom && p.bottom->currentLOD >= 0 &&
             p.currentLOD < p.bottom->currentLOD && (vX % (1u << p.bottom->currentLOD)) != 0)
        vX -= vX % (1u << p.bottom->currentLOD);

    if (vX == 0 && p.left && p.left->currentLOD >= 0 && p.currentLOD < p.left->currentLOD &&
        (vZ % (1u << p.left->currentLOD)) != 0)
        vZ -= vZ % (1u << p.left->currentLOD);
    else if (vX == cs && p.right && p.right->currentLOD >= 0 &&
             p.currentLOD < p.right->currentLOD && (vZ % (1u << p.right->currentLOD)) != 0)
        vZ -= vZ % (1u << p.right->currentLOD);

    if (vZ >= (u32)m_patchSize) vZ = cs;
    if (vX >= (u32)m_patchSize) vX = cs;

    return (vZ + (u32)(m_calcPatchSize * patchZ)) * (u32)m_size +
           (vX + (u32)(m_calcPatchSize * patchX));
}

// ── per-frame update ──

void TerrainLodNode::_update(float dt)
{
    (void)dt;
    if (m_patches.empty()) return;

    if (pre_render_lod())
    {
        // Rebuild the IBO from the new LOD selection
        m_indicesToRender = 0;
        int patchIdx = 0;
        for (int i = 0; i < m_patchCount; ++i)
        {
            for (int j = 0; j < m_patchCount; ++j, ++patchIdx)
            {
                int step = 1 << m_patches[patchIdx].currentLOD;
                int x = 0, z = 0;
                while (z < m_calcPatchSize)
                {
                    u32 i11 = get_index(j, i, patchIdx, (u32)x, (u32)z);
                    u32 i21 = get_index(j, i, patchIdx, (u32)(x + step), (u32)z);
                    u32 i12 = get_index(j, i, patchIdx, (u32)x, (u32)(z + step));
                    u32 i22 = get_index(j, i, patchIdx, (u32)(x + step), (u32)(z + step));

                    if (m_indicesToRender + 6 <= m_maxIndices)
                    {
                        m_indices[m_indicesToRender++] = i11;
                        m_indices[m_indicesToRender++] = i12;
                        m_indices[m_indicesToRender++] = i22;
                        m_indices[m_indicesToRender++] = i11;
                        m_indices[m_indicesToRender++] = i22;
                        m_indices[m_indicesToRender++] = i21;
                    }
                    x += step;
                    if (x >= m_calcPatchSize)
                    {
                        x = 0;
                        z += step;
                    }
                }
            }
        }
        m_terrainMesh.update_indices(m_indices.data(), m_indicesToRender);
        m_terrainMesh.set_dynamic_index_count(m_indicesToRender);
    }
}

// ── CPU queries ──

float TerrainLodNode::height_at(float wx, float wz) const
{
    if (!m_heightData || m_verts.empty()) return 0.f;
    float lx = Clamp((wx - m_pos.x) / m_scale.x, 0.f, 1.f);
    float lz = Clamp((wz - m_pos.z) / m_scale.z, 0.f, 1.f);
    float gx = lx * (m_size - 1), gz = lz * (m_size - 1);
    int ix = std::min((int)gx, m_size - 2), iz = std::min((int)gz, m_size - 2);
    float fx = gx - ix, fz = gz - iz;
    auto wy = [&](int x, int z) { return m_verts[(size_t)z * m_size + x].position.y; };
    return wy(ix, iz) * (1 - fx) * (1 - fz) + wy(ix + 1, iz) * fx * (1 - fz) +
           wy(ix, iz + 1) * (1 - fx) * fz + wy(ix + 1, iz + 1) * fx * fz;
}

Vec3 TerrainLodNode::normal_at(float wx, float wz) const
{
    if (m_verts.empty()) return Vec3(0, 1, 0);
    int x = (int)lroundf(Clamp((wx - m_pos.x) / m_scale.x, 0.f, 1.f) * (m_size - 1));
    int z = (int)lroundf(Clamp((wz - m_pos.z) / m_scale.z, 0.f, 1.f) * (m_size - 1));
    x = std::max(0, std::min(x, m_size - 1));
    z = std::max(0, std::min(z, m_size - 1));
    return m_verts[(size_t)z * m_size + x].normal;
}

// ── editing ──

void TerrainLodNode::modify_height(float wx, float wz, float delta, float radius)
{
    if (!m_heightData) return;
    float cx = (wx - m_pos.x) / m_scale.x * (m_size - 1);
    float cz = (wz - m_pos.z) / m_scale.z * (m_size - 1);
    float rx = radius / m_scale.x * (m_size - 1);
    float rz = radius / m_scale.z * (m_size - 1);
    float mr = std::max(rx, rz);
    float rawDelta = delta / (m_heightScale * m_scale.y);
    int minX = std::max(0, (int)(cx - mr)), maxX = std::min(m_size - 1, (int)(cx + mr));
    int minZ = std::max(0, (int)(cz - mr)), maxZ = std::min(m_size - 1, (int)(cz + mr));

    for (int z = minZ; z <= maxZ; ++z)
    {
        for (int x = minX; x <= maxX; ++x)
        {
            float dx = (x - cx) / rx, dz = (z - cz) / rz;
            float ds = dx * dx + dz * dz;
            if (ds > 1.f) continue;
            float w = cosf(ds * 1.5707963f);
            m_heightData[z * m_size + x] =
                Clamp(m_heightData[z * m_size + x] + rawDelta * w, 0.f, 1.f);
        }
    }
    for (int z = 0; z < m_size; ++z)
        for (int x = 0; x < m_size; ++x)
            m_sourceVerts[(size_t)z * m_size + x].position.y =
                m_heightData[z * m_size + x] * m_heightScale;

    bake_positions();
    calculate_normals();
    m_terrainMesh.update_vertices(m_verts.data(), (u32)m_verts.size());
    m_forceRecalc = true;
}

void TerrainLodNode::smooth_area(float wx, float wz, float radius, int iterations)
{
    if (!m_heightData) return;
    float cx = (wx - m_pos.x) / m_scale.x * (m_size - 1);
    float cz = (wz - m_pos.z) / m_scale.z * (m_size - 1);
    float rx = radius / m_scale.x * (m_size - 1);
    float rz = radius / m_scale.z * (m_size - 1);
    float mr = std::max(rx, rz);
    int minX = std::max(1, (int)(cx - mr)), maxX = std::min(m_size - 2, (int)(cx + mr));
    int minZ = std::max(1, (int)(cz - mr)), maxZ = std::min(m_size - 2, (int)(cz + mr));

    std::vector<float> h(m_heightData, m_heightData + (size_t)m_size * m_size);
    for (int it = 0; it < iterations; ++it)
    {
        std::vector<float> sm = h;
        for (int z = minZ; z <= maxZ; ++z)
        {
            for (int x = minX; x <= maxX; ++x)
            {
                float dx = (x - cx) / rx, dz = (z - cz) / rz;
                float ds = dx * dx + dz * dz;
                if (ds > 1.f) continue;
                int i = z * m_size + x;
                float avg = (h[i] * 4 + h[i - 1] * 2 + h[i + 1] * 2 + h[i - m_size] * 2 +
                             h[i + m_size] * 2 + h[i - m_size - 1] + h[i - m_size + 1] +
                             h[i + m_size - 1] + h[i + m_size + 1]) /
                            16.f;
                sm[i] = h[i] + (avg - h[i]) * (1.f - ds);
            }
        }
        h = sm;
    }
    for (int z = minZ; z <= maxZ; ++z)
        for (int x = minX; x <= maxX; ++x)
        {
            float dx = (x - cx) / rx, dz = (z - cz) / rz;
            if (dx * dx + dz * dz <= 1.f) m_heightData[z * m_size + x] = h[z * m_size + x];
        }
    for (int z = 0; z < m_size; ++z)
        for (int x = 0; x < m_size; ++x)
            m_sourceVerts[(size_t)z * m_size + x].position.y =
                m_heightData[z * m_size + x] * m_heightScale;

    bake_positions();
    calculate_normals();
    m_terrainMesh.update_vertices(m_verts.data(), (u32)m_verts.size());
    m_forceRecalc = true;
}
