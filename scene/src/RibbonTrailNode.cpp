#include "scene/RibbonTrailNode.hpp"
#include "scene/Math.hpp"
#include <algorithm>
#include <cmath>

// ── helpers ──
static Vec4 lerpV4(const Vec4& a, const Vec4& b, float t)
{
    return Vec4(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t,
                a.w + (b.w - a.w) * t);
}

RibbonTrailNode::RibbonTrailNode(const std::string& name, int maxChains, int maxElementsPerChain)
    : Node3D(name), m_maxChains(maxChains), m_maxElements(maxElementsPerChain)
{
    m_type = NT_RIBBONTRAIL;
    m_chains.reserve((size_t)maxChains);
}

int RibbonTrailNode::addChain(Node3D* emitter, const Vec4& startColor, const Vec4& endColor,
                               float startWidth, float endWidth)
{
    if ((int)m_chains.size() >= m_maxChains || !emitter) return -1;
    Chain ch;
    ch.emitter = emitter;
    ch.startColor = startColor;
    ch.endColor = endColor;
    ch.startWidth = startWidth;
    ch.endWidth = endWidth;
    ch.elements.reserve((size_t)m_maxElements);
    m_chains.push_back(ch);
    return (int)m_chains.size() - 1;
}

void RibbonTrailNode::clearChains() { m_chains.clear(); }

// ── per-frame simulation: record the emitter's world position ──
void RibbonTrailNode::_update(float dt)
{
    m_time += dt;
    for (Chain& ch : m_chains)
    {
        if (!ch.active || !ch.emitter) continue;

        // age existing elements
        for (Element& e : ch.elements)
            e.age += dt;

        // remove expired
        while (!ch.elements.empty() && ch.elements.front().age > m_trailLength)
            ch.elements.erase(ch.elements.begin());

        // record the emitter's current world position
        Vec3 worldPos = ch.emitter->get_global_position();
        Element newEl;
        newEl.position = worldPos;
        newEl.age = 0.f;

        if (!ch.elements.empty())
        {
            float dist = (worldPos - ch.elements.back().position).length();
            if (dist < m_minSeg) continue; // too close, skip
        }
        ch.elements.push_back(newEl);

        // cap
        while ((int)ch.elements.size() > m_maxElements)
            ch.elements.erase(ch.elements.begin());
    }
}

// ── GPU buffer management ──
bool RibbonTrailNode::ensure_gpu()
{
    if (m_gpu_ready) return true;

    // worst-case vertex/index count: each chain has m_maxElements elements,
    // each pair of consecutive elements produces 2 vertices (left/right of
    // the camera-facing quad) and the strip-indexing needs 2 tri per segment.
    // Allocated once, updated per frame via glBufferSubData (the same
    // strategy ParticleSystemNode uses).
    int maxVerts = m_maxChains * m_maxElements * 2;
    int maxIndices = m_maxChains * (m_maxElements - 1) * 6;

    if (maxVerts < 1) maxVerts = 1;
    if (maxIndices < 1) maxIndices = 6;

    std::vector<MeshVertex> verts((size_t)maxVerts);
    std::vector<u32>        idx((size_t)maxIndices);
    for (int i = 0; i < maxVerts; ++i)
    {
        verts[i].position = Vec3(0, 0, 0);
        verts[i].normal = Vec3(0, 1, 0);
        verts[i].tangent = Vec4(1, 1, 1, 1);
        verts[i].uv = Vec2(0, 0);
    }
    // static index pattern: each segment = 2 triangles (6 indices),
    // connecting 4 vertices from two consecutive element pairs
    for (int seg = 0; seg < m_maxChains * (m_maxElements - 1); ++seg)
    {
        u32 p0 = (u32)(seg * 2);     // prev left
        u32 p1 = (u32)(seg * 2 + 1); // prev right
        u32 c0 = (u32)(seg * 2 + 2); // curr left
        u32 c1 = (u32)(seg * 2 + 3); // curr right
        u32* tri = &idx[(size_t)seg * 6];
        tri[0] = p0;
        tri[1] = p1;
        tri[2] = c0;
        tri[3] = c1;
        tri[4] = c0;
        tri[5] = p1;
    }

    m_mesh.set_data(verts.data(), maxVerts, idx.data(), (int)idx.size());
    m_mesh.upload_dynamic();
    m_gpu_ready = true;
    return true;
}

// ── build the camera-facing quad strip from the trail history ──
void RibbonTrailNode::rebuild(const Vec3& camPos, const Vec3& camUp)
{
    if (!m_gpu_ready && !ensure_gpu()) { m_indexCount = 0; return; }

    int maxVerts = m_maxChains * m_maxElements * 2;
    if (maxVerts < 1) { m_indexCount = 0; return; }

    // build into local scratch (full allocation, padded with zeros)
    static std::vector<MeshVertex> verts;
    verts.assign((size_t)maxVerts, MeshVertex{});
    int vi = 0;

    for (Chain& ch : m_chains)
    {
        if (!ch.active) continue;
        int n = (int)ch.elements.size();
        if (n < 2) continue;

        for (int i = 0; i < n; ++i)
        {
            const Element& e = ch.elements[i];
            float t = e.age / m_trailLength;
            if (t > 1.f) t = 1.f;

            Vec4 col = lerpV4(ch.startColor, ch.endColor, t);
            float w = ch.startWidth + (ch.endWidth - ch.startWidth) * t;

            Vec3 toCam = camPos - e.position;
            Vec3 viewDir = toCam;
            float dlen = viewDir.length();
            if (dlen < 1e-4f) viewDir = Vec3(0, 0, 1);
            else viewDir = viewDir * (1.f / dlen);

            Vec3 right, up;
            if (i < n - 1)
            {
                Vec3 seg = (ch.elements[i + 1].position - e.position).normalized();
                right = Vec3::Cross(viewDir, seg).normalized();
                up = Vec3::Cross(right, viewDir).normalized();
            }
            else if (i > 0)
            {
                Vec3 seg = (e.position - ch.elements[i - 1].position).normalized();
                right = Vec3::Cross(viewDir, seg).normalized();
                up = Vec3::Cross(right, viewDir).normalized();
            }
            else
            {
                right = Vec3::Cross(viewDir, camUp).normalized();
                up = camUp;
            }

            // left vertex
            verts[vi].position = e.position - right * w;
            verts[vi].normal = up;
            verts[vi].tangent = Vec4(col.x, col.y, col.z, col.w);
            verts[vi].uv = Vec2(0.f, t);
            ++vi;

            // right vertex
            verts[vi].position = e.position + right * w;
            verts[vi].normal = up;
            verts[vi].tangent = Vec4(col.x, col.y, col.z, col.w);
            verts[vi].uv = Vec2(1.f, t);
            ++vi;

            if (vi >= maxVerts) break;
        }
        if (vi >= maxVerts) break;
    }

    // index count: each segment between two element pairs = 6 indices
    // vi = number of vertices written; segments = vi/2 - 1 (since each
    // element produces 2 verts, and each segment connects two element pairs)
    int segments = (vi / 2) - 1;
    if (segments < 0) segments = 0;
    m_indexCount = (u32)(segments * 6);

    // upload the vertex data (glBufferSubData on the existing VBO)
    m_mesh.update_vertices(verts.data(), (u32)verts.size());
    m_mesh.set_dynamic_index_count(m_indexCount);
}
