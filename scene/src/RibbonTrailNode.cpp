#include "scene/RibbonTrailNode.hpp"
#include "scene/Math.hpp"
#include <algorithm>
#include <cmath>

static Vec4 lerpV4(const Vec4& a, const Vec4& b, float t)
{
    return Vec4(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t,
                a.w + (b.w - a.w) * t);
}
static inline u16 nextRing(u16 idx, int maxEl) { return (idx + 1) % (u16)maxEl; }
static inline u16 prevRing(u16 idx, int maxEl) { return idx == 0 ? (u16)(maxEl - 1) : (u16)(idx - 1); }

RibbonTrailNode::RibbonTrailNode(const std::string& name, int maxChains, int maxElementsPerChain)
    : Node3D(name), m_maxChains(maxChains), m_maxElementsPerChain(maxElementsPerChain)
{
    m_type = NT_RIBBONTRAIL;
    if (m_maxChains > MAX_CHAINS) m_maxChains = MAX_CHAINS;
    if (m_maxChains < 1) m_maxChains = 1;
    m_elements.resize((size_t)m_maxChains * (size_t)m_maxElementsPerChain);
    m_elemLength = m_trailLength / (float)m_maxElementsPerChain;
}

int RibbonTrailNode::addChain(Node3D* emitter, const Vec4& startColor, const Vec4& endColor,
                               float startWidth, float endWidth)
{
    if (m_activeChains >= m_maxChains || !emitter) return -1;
    Chain& ch = m_chains[m_activeChains];
    ch.emitter = emitter;
    ch.startColor = startColor;
    ch.endColor = endColor;
    ch.startWidth = startWidth;
    ch.endWidth = endWidth;
    ch.active = true;
    resetTrail(ch);
    return m_activeChains++;
}

void RibbonTrailNode::clearChains()
{
    for (int i = 0; i < m_activeChains; ++i)
        m_chains[i].active = false;
    m_activeChains = 0;
}

void RibbonTrailNode::setTrailLength(float seconds)
{
    m_trailLength = seconds > 0.01f ? seconds : 0.01f;
    m_elemLength = m_trailLength / (float)m_maxElementsPerChain;
}

// ── seed the ring buffer with one element at the emitter position ──
void RibbonTrailNode::resetTrail(Chain& ch)
{
    if (!ch.emitter) return;
    Vec3 pos = ch.emitter->get_global_position();
    int base = m_activeChains * m_maxElementsPerChain;

    m_elements[base].position = pos;
    m_elements[base].color = ch.startColor;
    m_elements[base].width = ch.startWidth;

    ch.head = 0;
    ch.tail = 0;
    ch.count = 1;
    ch.lastBakedPos = pos;
}

// ── per-frame: evenly-spaced element baking (Ogre-style ring buffer) ──
void RibbonTrailNode::_update(float dt)
{
    (void)dt;
    for (int ci = 0; ci < m_activeChains; ++ci)
    {
        Chain& ch = m_chains[ci];
        if (!ch.active || !ch.emitter) continue;

        Vec3 worldPos = ch.emitter->get_global_position();
        int base = ci * m_maxElementsPerChain;
        int maxEl = m_maxElementsPerChain;
        float sqElem = m_elemLength * m_elemLength;

        if (ch.count == 1)
        {
            // seed exists; emitter has moved far enough → bake first segment
            float dx = worldPos.x - ch.lastBakedPos.x;
            float dy = worldPos.y - ch.lastBakedPos.y;
            float dz = worldPos.z - ch.lastBakedPos.z;
            if (dx * dx + dy * dy + dz * dz < sqElem * 0.25f) continue;

            Vec3 dir(worldPos.x - ch.lastBakedPos.x, worldPos.y - ch.lastBakedPos.y,
                     worldPos.z - ch.lastBakedPos.z);
            float dlen = sqrtf(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);
            dir = Vec3(dir.x / dlen, dir.y / dlen, dir.z / dlen);
            Vec3 baked(worldPos.x - dir.x * m_elemLength, worldPos.y - dir.y * m_elemLength,
                       worldPos.z - dir.z * m_elemLength);

            m_elements[base + ch.head].position = baked;
            ch.lastBakedPos = baked;

            u16 newHead = nextRing(ch.head, maxEl);
            m_elements[base + newHead].position = worldPos;
            m_elements[base + newHead].color = ch.startColor;
            m_elements[base + newHead].width = ch.startWidth;
            ch.head = newHead;
            ch.count = 2;
            ch.lastBakedPos = worldPos;
            continue;
        }

        // normal case: extend head
        Element& headElem = m_elements[base + ch.head];
        float dx = worldPos.x - headElem.position.x;
        float dy = worldPos.y - headElem.position.y;
        float dz = worldPos.z - headElem.position.z;
        float sqDist = dx * dx + dy * dy + dz * dz;

        if (sqDist >= sqElem)
        {
            // bake: lock head at exactly m_elemLength from the emitter
            float dist = sqrtf(sqDist);
            float inv = 1.f / dist;
            Vec3 dir(dx * inv, dy * inv, dz * inv);
            headElem.position = Vec3(worldPos.x - dir.x * m_elemLength,
                                     worldPos.y - dir.y * m_elemLength,
                                     worldPos.z - dir.z * m_elemLength);

            u16 newHead = nextRing(ch.head, maxEl);
            if (ch.count >= (u16)maxEl)
            {
                // buffer full → shrink tail smoothly (Ogre fade-out trick)
                u16 preTail = prevRing(ch.tail, maxEl);
                if (preTail != ch.head)
                {
                    Element& tailElem = m_elements[base + ch.tail];
                    Element& preElem = m_elements[base + preTail];
                    float tdx = tailElem.position.x - preElem.position.x;
                    float tdy = tailElem.position.y - preElem.position.y;
                    float tdz = tailElem.position.z - preElem.position.z;
                    float tlen = tdx * tdx + tdy * tdy + tdz * tdz;
                    if (tlen > 1e-12f)
                    {
                        tlen = sqrtf(tlen);
                        float shrink = m_elemLength - dist;
                        if (shrink > 0.f)
                        {
                            if (shrink > tlen) shrink = tlen;
                            float s = shrink / tlen;
                            tailElem.position = Vec3(preElem.position.x + tdx * s,
                                                     preElem.position.y + tdy * s,
                                                     preElem.position.z + tdz * s);
                        }
                    }
                }
                ch.tail = nextRing(ch.tail, maxEl);
            }
            else
            {
                ch.count++;
            }

            m_elements[base + newHead].position = worldPos;
            m_elements[base + newHead].color = ch.startColor;
            m_elements[base + newHead].width = ch.startWidth;
            ch.head = newHead;
            ch.lastBakedPos = worldPos;
        }
        else
        {
            // just nudge the head to the current position
            headElem.position = worldPos;
        }
    }
}

// ── GPU: allocate once with upload_dynamic() ──
bool RibbonTrailNode::ensure_gpu()
{
    if (m_gpu_ready) return true;

    int maxVerts = m_maxChains * m_maxElementsPerChain * 2;
    int maxIndices = m_maxChains * (m_maxElementsPerChain - 1) * 6;
    if (maxVerts < 2) maxVerts = 2;
    if (maxIndices < 6) maxIndices = 6;

    std::vector<MeshVertex> verts((size_t)maxVerts);
    std::vector<u32> idx((size_t)maxIndices);

    // pre-build static strip index pattern (never changes)
    for (int seg = 0; seg < m_maxChains * (m_maxElementsPerChain - 1); ++seg)
    {
        u32 p0 = (u32)(seg * 2), p1 = (u32)(seg * 2 + 1);
        u32 c0 = (u32)(seg * 2 + 2), c1 = (u32)(seg * 2 + 3);
        u32* tri = &idx[(size_t)seg * 6];
        tri[0] = p0; tri[1] = p1; tri[2] = c0;
        tri[3] = c1; tri[4] = c0; tri[5] = p1;
    }

    m_mesh.set_data(verts.data(), maxVerts, idx.data(), (int)idx.size());
    m_mesh.upload_dynamic();
    m_scratchVerts.resize((size_t)maxVerts);
    m_gpu_ready = true;
    return true;
}

// ── walk ring buffer and bake camera-facing quads ──
void RibbonTrailNode::rebuild(const Vec3& camPos, const Vec3& camUp)
{
    if (!m_gpu_ready && !ensure_gpu()) { m_indexCount = 0; return; }

    int maxVerts = m_maxChains * m_maxElementsPerChain * 2;
    if (maxVerts < 2) { m_indexCount = 0; return; }

    // zero the scratch (std::fill is faster than assign-with-value for POD)
    std::fill(m_scratchVerts.begin(), m_scratchVerts.end(), MeshVertex{});

    int vi = 0; // global vertex index across all chains

    for (int ci = 0; ci < m_activeChains; ++ci)
    {
        Chain& ch = m_chains[ci];
        int n = ch.count;
        if (!ch.active || n < 2) continue;

        int base = ci * m_maxElementsPerChain;
        int maxEl = m_maxElementsPerChain;

        for (int i = 0; i < n; ++i)
        {
            u16 ei = (u16)((ch.tail + i) % maxEl);
            const Element& e = m_elements[base + ei];

            // t=0 at tail (oldest/fading), t=1 at head (newest/brightest)
            float t = (float)i / (float)(n - 1);

            Vec4 col = lerpV4(ch.endColor, ch.startColor, t);
            float w = ch.endWidth + (ch.startWidth - ch.endWidth) * t;

            Vec3 toCam(camPos.x - e.position.x, camPos.y - e.position.y,
                       camPos.z - e.position.z);
            float dlen = toCam.x * toCam.x + toCam.y * toCam.y + toCam.z * toCam.z;
            Vec3 viewDir;
            if (dlen < 1e-8f)
                viewDir = Vec3(0, 0, 1);
            else
            {
                float inv = 1.f / sqrtf(dlen);
                viewDir = Vec3(toCam.x * inv, toCam.y * inv, toCam.z * inv);
            }

            // segment-direction-based quad orientation (smoother than camUp)
            Vec3 right, up;
            if (i < n - 1)
            {
                u16 ni = (u16)((ch.tail + i + 1) % maxEl);
                const Element& next = m_elements[base + ni];
                float sx = next.position.x - e.position.x;
                float sy = next.position.y - e.position.y;
                float sz = next.position.z - e.position.z;
                float sl = sx * sx + sy * sy + sz * sz;
                if (sl > 1e-12f)
                {
                    sl = 1.f / sqrtf(sl);
                    Vec3 seg(sx * sl, sy * sl, sz * sl);
                    right = Vec3::Cross(viewDir, seg).normalized();
                    up = Vec3::Cross(right, viewDir).normalized();
                }
                else { right = Vec3::Cross(viewDir, camUp).normalized(); up = camUp; }
            }
            else if (i > 0)
            {
                u16 pi = (u16)((ch.tail + i - 1) % maxEl);
                const Element& prev = m_elements[base + pi];
                float sx = e.position.x - prev.position.x;
                float sy = e.position.y - prev.position.y;
                float sz = e.position.z - prev.position.z;
                float sl = sx * sx + sy * sy + sz * sz;
                if (sl > 1e-12f)
                {
                    sl = 1.f / sqrtf(sl);
                    Vec3 seg(sx * sl, sy * sl, sz * sl);
                    right = Vec3::Cross(viewDir, seg).normalized();
                    up = Vec3::Cross(right, viewDir).normalized();
                }
                else { right = Vec3::Cross(viewDir, camUp).normalized(); up = camUp; }
            }
            else
            {
                right = Vec3::Cross(viewDir, camUp).normalized();
                up = camUp;
            }

            // left
            m_scratchVerts[vi].position =
                Vec3(e.position.x - right.x * w, e.position.y - right.y * w,
                     e.position.z - right.z * w);
            m_scratchVerts[vi].normal = up;
            m_scratchVerts[vi].tangent = Vec4(col.x, col.y, col.z, col.w);
            m_scratchVerts[vi].uv = Vec2(0.f, t);
            ++vi;

            // right
            m_scratchVerts[vi].position =
                Vec3(e.position.x + right.x * w, e.position.y + right.y * w,
                     e.position.z + right.z * w);
            m_scratchVerts[vi].normal = up;
            m_scratchVerts[vi].tangent = Vec4(col.x, col.y, col.z, col.w);
            m_scratchVerts[vi].uv = Vec2(1.f, t);
            ++vi;

            if (vi + 2 > maxVerts) break;
        }
        if (vi + 2 > maxVerts) break;
    }

    int segments = (vi / 2) - 1;
    if (segments < 0) segments = 0;
    m_indexCount = (u32)(segments * 6);

    m_mesh.update_vertices(m_scratchVerts.data(), (u32)m_scratchVerts.size());
    m_mesh.set_dynamic_index_count(m_indexCount);
}
