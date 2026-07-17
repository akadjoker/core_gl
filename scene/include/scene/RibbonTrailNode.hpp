#pragma once

#include "scene/Mesh.hpp"
#include "scene/Node3D.hpp"
#include "scene/ParticleSystemNode.hpp" // ParticleBlendMode
#include <vector>

namespace gl
{
class Texture;
}

// Ogre-style ribbon trail: each "chain" follows an emitter Node3D and
// records a history of world positions in a ring buffer. Elements are
// evenly spaced (trailLength / maxElements) — when the emitter moves far
// enough, a new element is "baked"; when the buffer is full, the tail
// smoothly shrinks for a graceful fade-out instead of popping.
//
// On rebuild (called by the renderer once the camera basis is known), a
// strip of camera-facing quads is baked into a dynamic Mesh with color
// riding in the tangent slot — reusing the particle shader. Drawn in the
// transparent pass with depth-write off.
class RibbonTrailNode : public Node3D
{
public:
    static constexpr NodeType ClassType = NT_RIBBONTRAIL;
    static constexpr u16   CHAIN_EMPTY = 0xFFFF;

    struct Element
    {
        Vec3 position{0, 0, 0};
        Vec3 tip{0, 0, 0}; // blade chains only: 2nd point (position = hilt)
        Vec4 color{1, 1, 1, 1};
        float width = 0.5f;
    };

    struct Chain
    {
        Node3D*       emitter = nullptr;
        Node3D*       tipEmitter = nullptr; // non-null → blade mode
        Vec4          startColor{1, 1, 1, 1};
        Vec4          endColor{1, 1, 1, 0};
        float         startWidth = 0.7f;
        float         endWidth = 0.05f;
 
        float         fadeTime = 0.3f;
        bool          active = true;
        // ring buffer state
        u16           head = CHAIN_EMPTY; // index of newest element
        u16           tail = CHAIN_EMPTY; // index of oldest element
        u16           count = 0;
        Vec3          lastBakedPos{0, 0, 0};
    };

    explicit RibbonTrailNode(const std::string& name = "ribbon", int maxChains = 4,
                             int maxElementsPerChain = 48);
    ~RibbonTrailNode() override = default;

    bool is_a(NodeType t) const override { return t == NT_RIBBONTRAIL || Node3D::is_a(t); }

    // ── chain management ──
    // fadeTime: seconds for a baked element to go from start->end
    // color/width (Ogre's setWidthChange/setColourChange) — independent
    // of setTrailLength(), which only governs baking spacing.
    int  addChain(Node3D* emitter, const Vec4& startColor = Vec4(1, 1, 1, 1),
                  const Vec4& endColor = Vec4(1, 1, 1, 0), float startWidth = 0.7f,
                  float endWidth = 0.05f, float fadeTime = 0.3f);

    // blade ("sword swipe") chain: instead of a camera-facing ribbon
    // following one point, each sample records TWO points — hilt (base)
    // and tip — and quads span the full blade between consecutive
    // samples, filling the swept arc. width acts as a 0..1 fraction of
    // the hilt→tip span (startSpan 1 → endSpan 0 shrinks the trail back
    // toward the hilt as it fades). Baking distance is measured at the
    // tip, which sweeps the farthest.
    int  addBladeChain(Node3D* base, Node3D* tip,
                       const Vec4& startColor = Vec4(1, 1, 1, 1),
                       const Vec4& endColor = Vec4(1, 1, 1, 0),
                       float fadeTime = 0.3f, float startSpan = 1.f, float endSpan = 0.f);
    void clearChains();

    // emission gate: while off, no new elements are baked (idle bone
    // sway won't keep smearing a stub trail) but existing ones still
    // fade/expire, so the trail vanishes smoothly instead of popping.
    // Turning back on re-seeds every chain at its emitter's current
    // position so the trail doesn't lance across from where it stopped.
    void setEmitting(bool on);
    bool emitting() const { return m_emitting; }

    // ── parameters ──
    // spatial extent the ring buffer can span (Ogre: mTrailLength) — how
    // far apart baked elements are (trailLength / maxElementsPerChain),
    // NOT how long they take to fade (see addChain's fadeTime).
    void setTrailLength(float seconds);

    // blade chains only: Catmull-Rom subdivisions per baked segment
    // (1 = raw faceted samples). Smooths the swept arc so fast swings
    // don't show flat quad "folds". Call before first render.
    void setSmoothing(int subdivisions);

    // ── renderer-side ──
    bool ensure_gpu();
    void rebuild(const Vec3& camPos, const Vec3& camUp);
    Mesh& quad_mesh() { return m_mesh; }
    u32   index_count() const { return m_indexCount; }

    gl::Texture*      texture = nullptr;
    ParticleBlendMode blend = ParticleBlendMode::Additive;
    // stylized swooshes usually want depth test OFF: with it on, the
    // sweep surface gets clipped where it dips into the character/blade,
    // leaving hard seams between quads
    bool              depthTest = true;

protected:
    void _update(float dt) override;
    void _release_gpu() override { m_mesh.release_gpu(); }

private:
    void resetTrail(Chain& ch, int chainIndex);

    static constexpr int MAX_CHAINS = 4;
    Chain  m_chains[MAX_CHAINS];
    int    m_activeChains = 0;
    bool   m_emitting = true;

    // ring buffer: one flat array shared by all chains
    std::vector<Element> m_elements; // size = maxChains * maxElements
    int m_maxChains;
    int m_maxElementsPerChain;
    float m_elemLength = 0.025f; // trailLength / maxElements
    float m_trailLength = 1.2f;
    int   m_subdiv = 4; // blade smoothing: Catmull-Rom steps per segment

    Mesh  m_mesh;
    bool  m_gpu_ready = false;
    u32   m_indexCount = 0;

    // scratch reused across rebuild() calls
    std::vector<MeshVertex> m_scratchVerts;
    std::vector<u32> m_scratchIndices;
    // blade smoothing scratch: ordered (tail→head) copies of the live
    // ring samples, so Catmull-Rom can index neighbours without ring math
    std::vector<Vec3>  m_smoothHilt;
    std::vector<Vec3>  m_smoothTip;
    std::vector<Vec4>  m_smoothCol;
    std::vector<float> m_smoothW;
};
