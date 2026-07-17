#include "scene/RibbonTrailNode.hpp"
#include "scene/Math.hpp"
#include <algorithm>
#include <cmath>

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
                               float startWidth, float endWidth, float fadeTime)
{
    if (m_activeChains >= m_maxChains || !emitter) return -1;
    Chain& ch = m_chains[m_activeChains];
    ch.emitter = emitter;
    ch.startColor = startColor;
    ch.endColor = endColor;
    ch.startWidth = startWidth;
    ch.endWidth = endWidth;
    ch.fadeTime = fadeTime;;
    ch.tipEmitter = nullptr;
    ch.active = true;
    resetTrail(ch, m_activeChains);
    return m_activeChains++;
}

int RibbonTrailNode::addBladeChain(Node3D* base, Node3D* tip, const Vec4& startColor,
                                    const Vec4& endColor, float fadeTime, float startSpan,
                                    float endSpan)
{
    if (m_activeChains >= m_maxChains || !base || !tip) return -1;
    Chain& ch = m_chains[m_activeChains];
    ch.emitter = base;
    ch.tipEmitter = tip;
    ch.startColor = startColor;
    ch.endColor = endColor;
    // width doubles as the hilt→tip span fraction in blade mode
    ch.startWidth = startSpan;
    ch.endWidth = endSpan;
    ch.fadeTime = fadeTime;
    ch.active = true;
    resetTrail(ch, m_activeChains);
    return m_activeChains++;
}

void RibbonTrailNode::setEmitting(bool on)
{
    if (on == m_emitting) return;
    m_emitting = on;
    if (on)
    {
        // re-seed at current emitter positions so the first baked segment
        // doesn't lance across from wherever the trail last stopped
        for (int i = 0; i < m_activeChains; ++i)
            if (m_chains[i].active) resetTrail(m_chains[i], i);
    }
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

void RibbonTrailNode::setSmoothing(int subdivisions)
{
    if (subdivisions < 1) subdivisions = 1;
    if (subdivisions > 8) subdivisions = 8;
    if (subdivisions == m_subdiv) return;
    m_subdiv = subdivisions;
    // GPU buffers are sized for the subdivided edge count → realloc
    if (m_gpu_ready)
    {
        m_mesh.release_gpu();
        m_gpu_ready = false;
    }
}

// centripetal-flavoured uniform Catmull-Rom between p1 and p2
static inline Vec3 catmullRom(const Vec3& p0, const Vec3& p1, const Vec3& p2, const Vec3& p3,
                              float t)
{
    float t2 = t * t;
    float t3 = t2 * t;
    return Vec3(
        0.5f * (2.f * p1.x + (p2.x - p0.x) * t +
                (2.f * p0.x - 5.f * p1.x + 4.f * p2.x - p3.x) * t2 +
                (3.f * p1.x - 3.f * p2.x + p3.x - p0.x) * t3),
        0.5f * (2.f * p1.y + (p2.y - p0.y) * t +
                (2.f * p0.y - 5.f * p1.y + 4.f * p2.y - p3.y) * t2 +
                (3.f * p1.y - 3.f * p2.y + p3.y - p0.y) * t3),
        0.5f * (2.f * p1.z + (p2.z - p0.z) * t +
                (2.f * p0.z - 5.f * p1.z + 4.f * p2.z - p3.z) * t2 +
                (3.f * p1.z - 3.f * p2.z + p3.z - p0.z) * t3));
}

// ── seed the ring buffer with one element at the emitter position ──
void RibbonTrailNode::resetTrail(Chain& ch, int chainIndex)
{
    if (!ch.emitter) return;
    // blade mode tracks the tip (it sweeps the farthest); ribbon mode
    // tracks the single emitter point
    Vec3 pos = ch.tipEmitter ? ch.tipEmitter->get_global_position()
                             : ch.emitter->get_global_position();
    int base = chainIndex * m_maxElementsPerChain;

    m_elements[base].position = ch.tipEmitter ? ch.emitter->get_global_position() : pos;
    m_elements[base].tip = pos;
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
    for (int ci = 0; m_emitting && ci < m_activeChains; ++ci)
    {
        Chain& ch = m_chains[ci];
        if (!ch.active || !ch.emitter) continue;

        // blade mode: bake by tip motion, remember the hilt alongside
        const bool blade = ch.tipEmitter != nullptr;
        Vec3 worldPos = blade ? ch.tipEmitter->get_global_position()
                              : ch.emitter->get_global_position();
        Vec3 basePos = blade ? ch.emitter->get_global_position() : worldPos;
        int base = ci * m_maxElementsPerChain;
        int maxEl = m_maxElementsPerChain;
        float sqElem = m_elemLength * m_elemLength;

        // the point the baking logic tracks: tip for blades, the single
        // emitter point for ribbons
        auto trackOf = [&](Element& e) -> Vec3& { return blade ? e.tip : e.position; };

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

            trackOf(m_elements[base + ch.head]) = baked;
            if (blade) m_elements[base + ch.head].position = basePos;
            ch.lastBakedPos = baked;

            u16 newHead = nextRing(ch.head, maxEl);
            trackOf(m_elements[base + newHead]) = worldPos;
            if (blade) m_elements[base + newHead].position = basePos;
            m_elements[base + newHead].color = ch.startColor;
            m_elements[base + newHead].width = ch.startWidth;
            ch.head = newHead;
            ch.count = 2;
            ch.lastBakedPos = worldPos;
            continue;
        }

        // normal case: extend head. A `while` here (not a single `if`) is
        // load-bearing — Ogre's own updateTrail() loops "repeat this
        // entire process if chain is stretched beyond its natural length"
        // for exactly this reason: a fast swing can cover more than
        // m_elemLength in a single frame, and baking only one segment
        // would leave a long straight gap instead of a smooth curve.
        for (;;)
        {
            Element& headElem = m_elements[base + ch.head];
            Vec3& headTrack = trackOf(headElem);
            float dx = worldPos.x - headTrack.x;
            float dy = worldPos.y - headTrack.y;
            float dz = worldPos.z - headTrack.z;
            float sqDist = dx * dx + dy * dy + dz * dz;

            if (sqDist < sqElem)
            {
                // just nudge the head to the current position
                headTrack = worldPos;
                if (blade) headElem.position = basePos;
                break;
            }

            // bake: lock head at exactly m_elemLength from the emitter
            float dist = sqrtf(sqDist);
            float inv = 1.f / dist;
            Vec3 dir(dx * inv, dy * inv, dz * inv);
            headTrack = Vec3(worldPos.x - dir.x * m_elemLength,
                             worldPos.y - dir.y * m_elemLength,
                             worldPos.z - dir.z * m_elemLength);
            if (blade) headElem.position = basePos;

            u16 newHead = nextRing(ch.head, maxEl);
            if (ch.count >= (u16)maxEl)
            {
                // buffer full → shrink tail smoothly (Ogre fade-out trick)
                u16 preTail = prevRing(ch.tail, maxEl);
                if (preTail != ch.head)
                {
                    Element& tailElem = m_elements[base + ch.tail];
                    Element& preElem = m_elements[base + preTail];
                    Vec3& tailTrack = trackOf(tailElem);
                    Vec3& preTrack = trackOf(preElem);
                    float tdx = tailTrack.x - preTrack.x;
                    float tdy = tailTrack.y - preTrack.y;
                    float tdz = tailTrack.z - preTrack.z;
                    float tlen = tdx * tdx + tdy * tdy + tdz * tdz;
                    if (tlen > 1e-12f)
                    {
                        tlen = sqrtf(tlen);
                        float shrink = m_elemLength - dist;
                        if (shrink > 0.f)
                        {
                            if (shrink > tlen) shrink = tlen;
                            float s = shrink / tlen;
                            tailTrack = Vec3(preTrack.x + tdx * s,
                                             preTrack.y + tdy * s,
                                             preTrack.z + tdz * s);
                            if (blade)
                            {
                                // shrink the hilt edge with the same factor
                                // so the quad edge stays parallel-ish
                                tailElem.position = Vec3(
                                    preElem.position.x + (tailElem.position.x - preElem.position.x) * s,
                                    preElem.position.y + (tailElem.position.y - preElem.position.y) * s,
                                    preElem.position.z + (tailElem.position.z - preElem.position.z) * s);
                            }
                        }
                    }
                }
                ch.tail = nextRing(ch.tail, maxEl);
            }
            else
            {
                ch.count++;
            }

            trackOf(m_elements[base + newHead]) = worldPos;
            if (blade) m_elements[base + newHead].position = basePos;
            m_elements[base + newHead].color = ch.startColor;
            m_elements[base + newHead].width = ch.startWidth;
            ch.head = newHead;
            ch.lastBakedPos = worldPos;

            // after baking, if the new head is already close enough to
            // the emitter we're done; otherwise loop again (multiple
            // segments needed to cover this frame's motion)
            Vec3& nt = trackOf(m_elements[base + newHead]);
            float ndx = worldPos.x - nt.x;
            float ndy = worldPos.y - nt.y;
            float ndz = worldPos.z - nt.z;
            if (ndx * ndx + ndy * ndy + ndz * ndz <= sqElem) break;
            // safety: never let one frame bake more than a full buffer's
            // worth of segments (pathological teleport/huge dt)
            if (ch.count >= (u16)maxEl && ch.tail == nextRing(ch.head, maxEl)) break;
        }
    }

    // ── Ogre-style time fade (RibbonTrail::_timeUpdate): every element
    // except the live head continuously fades width/colour toward the end
    // values, at a fixed per-second rate — independent of whether the
    // emitter is still moving. Without this, a trail that stops moving
    // (sword comes to rest after a swing) just sits frozen at whatever
    // color/width its ring position happened to have, instead of shrinking
    // away — that was the bug: color/width used to be derived purely from
    // ring position at rebuild time, which never changes once baking stops.
    for (int ci = 0; ci < m_activeChains; ++ci)
    {
        Chain& ch = m_chains[ci];
        if (!ch.active || !ch.emitter || ch.count < 2) continue;

        int base = ci * m_maxElementsPerChain;
        int maxEl = m_maxElementsPerChain;
        // per-chain fade rate (Ogre's setWidthChange/setColourChange) —
        // independent of m_trailLength, which only sets baking spacing
        float fadeTime = ch.fadeTime > 1e-4f ? ch.fadeTime : 1e-4f;
        float deltaWidth = (ch.startWidth - ch.endWidth) / fadeTime;
        Vec4 deltaColor((ch.startColor.x - ch.endColor.x) / fadeTime,
                        (ch.startColor.y - ch.endColor.y) / fadeTime,
                        (ch.startColor.z - ch.endColor.z) / fadeTime,
                        (ch.startColor.w - ch.endColor.w) / fadeTime);

        // fade every live element EXCEPT the head (tail → head-1). The head
        // is "live" and keeps startColor/startWidth while it tracks the
        // emitter; everything behind it decays continuously even when the
        // emitter stops moving. When emission is gated off the head fades
        // too — nothing is live anymore, the whole trail dissolves.
        int nFade = m_emitting ? ch.count - 1 : ch.count;
        u16 e = ch.tail;
        for (int k = 0; k < nFade; ++k, e = nextRing(e, maxEl))
        {
            Element& el = m_elements[base + e];
            el.width -= deltaWidth * dt;
            if (el.width < 0.f) el.width = 0.f;
            el.color.x -= deltaColor.x * dt;
            el.color.y -= deltaColor.y * dt;
            el.color.z -= deltaColor.z * dt;
            el.color.w -= deltaColor.w * dt;
            if (el.color.x < 0.f) el.color.x = 0.f;
            if (el.color.y < 0.f) el.color.y = 0.f;
            if (el.color.z < 0.f) el.color.z = 0.f;
            if (el.color.w < 0.f) el.color.w = 0.f;
        }

        // expire fully-faded tail elements so a resting trail actually
        // clears (count shrinks) instead of keeping invisible quads alive
        while (ch.count > 1)
        {
            const Element& t = m_elements[base + ch.tail];
            bool invisible = t.width <= 0.f ||
                             (t.color.x <= 0.f && t.color.y <= 0.f &&
                              t.color.z <= 0.f && t.color.w <= 0.f);
            if (!invisible) break;
            ch.tail = nextRing(ch.tail, maxEl);
            ch.count--;
        }
    }
}

// ── GPU: allocate once with upload_dynamic() ──
bool RibbonTrailNode::ensure_gpu()
{
    if (m_gpu_ready) return true;

    // budget for the worst case: every chain a blade chain at full
    // subdivision ((n-1)*subdiv + 1 edges); plain ribbons use fewer
    int maxEdges = (m_maxElementsPerChain - 1) * m_subdiv + 1;
    int maxVerts = m_maxChains * maxEdges * 2;
    int maxIndices = m_maxChains * (maxEdges - 1) * 6;
    if (maxVerts < 2) maxVerts = 2;
    if (maxIndices < 6) maxIndices = 6;

    std::vector<MeshVertex> verts((size_t)maxVerts);
    std::vector<u32> idx((size_t)maxIndices, 0u);

    // indices are rebuilt every frame in rebuild() — chains are packed
    // densely (only as many vertices as each chain actually has live
    // elements, not a fixed maxElementsPerChain-sized slot), so a static
    // precomputed strip pattern can't know the real per-chain boundaries
    // and used to stitch the last vertex of one chain to the first vertex
    // of the next (visible as a spurious beam connecting two blades).
    m_mesh.set_data(verts.data(), maxVerts, idx.data(), (int)idx.size());
    m_mesh.upload_dynamic();
    m_scratchVerts.resize((size_t)maxVerts);
    m_scratchIndices.resize((size_t)maxIndices);
    m_gpu_ready = true;
    return true;
}

// ── walk ring buffer and bake camera-facing quads ──
void RibbonTrailNode::rebuild(const Vec3& camPos, const Vec3& camUp)
{
    if (!m_gpu_ready && !ensure_gpu()) { m_indexCount = 0; return; }

    int maxVerts = (int)m_scratchVerts.size(); // sized by ensure_gpu (incl. subdiv)
    if (maxVerts < 2) { m_indexCount = 0; return; }

    // zero the scratch (std::fill is faster than assign-with-value for POD)
    std::fill(m_scratchVerts.begin(), m_scratchVerts.end(), MeshVertex{});

    int vi = 0; // global vertex index across all chains
    int ii = 0; // global index-buffer cursor across all chains

    for (int ci = 0; ci < m_activeChains; ++ci)
    {
        Chain& ch = m_chains[ci];
        int n = ch.count;
        if (!ch.active || n < 2) continue;

        int base = ci * m_maxElementsPerChain;
        int maxEl = m_maxElementsPerChain;
        const bool bladeChain = ch.tipEmitter != nullptr;
        const int chainStartVi = vi; // dense packing: this chain's own local vertex range

        if (bladeChain)
        {
            // ── smoothed swept surface: Catmull-Rom through the baked
            // hilt/tip samples, m_subdiv edges per segment. Raw per-frame
            // samples are straight quads — a fast swing shows visible
            // "folds" at every sample where the arc bends; the spline
            // fills those with a smooth curve. ──
            m_smoothHilt.resize((size_t)n);
            m_smoothTip.resize((size_t)n);
            m_smoothCol.resize((size_t)n);
            m_smoothW.resize((size_t)n);
            for (int i = 0; i < n; ++i)
            {
                const Element& e = m_elements[base + (u16)((ch.tail + i) % maxEl)];
                m_smoothHilt[i] = e.position;
                m_smoothTip[i] = e.tip;
                m_smoothCol[i] = e.color;
                m_smoothW[i] = e.width;
            }

            const int edges = (n - 1) * m_subdiv + 1;
            for (int eIdx = 0; eIdx < edges && vi + 2 <= maxVerts; ++eIdx)
            {
                int seg = eIdx / m_subdiv;
                float f = (float)(eIdx % m_subdiv) / (float)m_subdiv;
                if (seg >= n - 1) { seg = n - 2; f = 1.f; } // final edge

                // clamped neighbour indices for the spline
                const int i0 = seg > 0 ? seg - 1 : 0;
                const int i3 = seg + 2 < n ? seg + 2 : n - 1;
                Vec3 hilt = catmullRom(m_smoothHilt[i0], m_smoothHilt[seg],
                                       m_smoothHilt[seg + 1], m_smoothHilt[i3], f);
                Vec3 tip = catmullRom(m_smoothTip[i0], m_smoothTip[seg],
                                      m_smoothTip[seg + 1], m_smoothTip[i3], f);

                // color/width lerp linearly between bracketing samples
                const Vec4& c0 = m_smoothCol[seg];
                const Vec4& c1 = m_smoothCol[seg + 1];
                Vec4 col(c0.x + (c1.x - c0.x) * f, c0.y + (c1.y - c0.y) * f,
                         c0.z + (c1.z - c0.z) * f, c0.w + (c1.w - c0.w) * f);
                float w = m_smoothW[seg] + (m_smoothW[seg + 1] - m_smoothW[seg]) * f;
                float span = w < 0.f ? 0.f : (w > 1.f ? 1.f : w);

                Vec3 bladeVec(tip.x - hilt.x, tip.y - hilt.y, tip.z - hilt.z);
                Vec3 tipPos(hilt.x + bladeVec.x * span, hilt.y + bladeVec.y * span,
                            hilt.z + bladeVec.z * span);

                // normal: blade dir × sweep dir of this segment
                Vec3 nrm(0.f, 1.f, 0.f);
                {
                    Vec3 sweep(m_smoothHilt[seg + 1].x - m_smoothHilt[seg].x,
                               m_smoothHilt[seg + 1].y - m_smoothHilt[seg].y,
                               m_smoothHilt[seg + 1].z - m_smoothHilt[seg].z);
                    Vec3 c = Vec3::Cross(bladeVec, sweep);
                    float cl = c.x * c.x + c.y * c.y + c.z * c.z;
                    if (cl > 1e-12f)
                    {
                        cl = 1.f / sqrtf(cl);
                        nrm = Vec3(c.x * cl, c.y * cl, c.z * cl);
                    }
                }

                float t = (float)eIdx / (float)(edges - 1);

                m_scratchVerts[vi].position = hilt; // hilt edge
                m_scratchVerts[vi].normal = nrm;
                m_scratchVerts[vi].tangent = Vec4(col.x, col.y, col.z, col.w);
                m_scratchVerts[vi].uv = Vec2(0.f, t);
                ++vi;

                m_scratchVerts[vi].position = tipPos; // tip edge
                m_scratchVerts[vi].normal = nrm;
                m_scratchVerts[vi].tangent = Vec4(col.x, col.y, col.z, col.w);
                m_scratchVerts[vi].uv = Vec2(1.f, t);
                ++vi;
            }
        }
        else
        for (int i = 0; i < n; ++i)
        {
            u16 ei = (u16)((ch.tail + i) % maxEl);
            const Element& e = m_elements[base + ei];
            float t = (float)i / (float)(n - 1); // v coordinate only

            // color/width are each element's own live, time-faded values
            // (see _update's Ogre-style per-frame decay) — not derived from
            // ring position, so a trail that stops moving still shrinks
            // away over trailLength seconds instead of freezing in place.
            Vec4 col = e.color;
            float w = e.width;

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

        // indices for THIS chain only, local to [chainStartVi, vi) — never
        // bridges into the next chain's vertex range. Built fresh every
        // frame because chains are packed densely (only as many vertices
        // as are actually alive, not a fixed maxElementsPerChain-sized
        // slot), so a precomputed static pattern can't know the real
        // per-chain boundaries (see tmp/core/src/RibbonTrail.cpp, which
        // rebuilds both buffers the same way every frame).
        int chainVerts = vi - chainStartVi;
        for (int s = 0; s + 3 < chainVerts && ii + 6 <= (int)m_scratchIndices.size(); s += 2)
        {
            u32 p0 = (u32)(chainStartVi + s), p1 = p0 + 1, c0 = p0 + 2, c1 = p0 + 3;
            m_scratchIndices[ii++] = p0; m_scratchIndices[ii++] = p1; m_scratchIndices[ii++] = c0;
            m_scratchIndices[ii++] = c1; m_scratchIndices[ii++] = c0; m_scratchIndices[ii++] = p1;
        }

        if (vi + 2 > maxVerts) break;
    }

    m_indexCount = (u32)ii;

    m_mesh.update_vertices(m_scratchVerts.data(), (u32)m_scratchVerts.size());
    m_mesh.update_indices(m_scratchIndices.data(), (u32)ii);
    m_mesh.set_dynamic_index_count(m_indexCount);
}
