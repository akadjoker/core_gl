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
        Vec4 color{1, 1, 1, 1};
        float width = 0.5f;
    };

    struct Chain
    {
        Node3D*       emitter = nullptr;
        Vec4          startColor{1, 1, 1, 1};
        Vec4          endColor{1, 1, 1, 0};
        float         startWidth = 0.7f;
        float         endWidth = 0.05f;
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
    int  addChain(Node3D* emitter, const Vec4& startColor = Vec4(1, 1, 1, 1),
                  const Vec4& endColor = Vec4(1, 1, 1, 0), float startWidth = 0.7f,
                  float endWidth = 0.05f);
    void clearChains();

    // ── parameters ──
    void setTrailLength(float seconds);

    // ── renderer-side ──
    bool ensure_gpu();
    void rebuild(const Vec3& camPos, const Vec3& camUp);
    Mesh& quad_mesh() { return m_mesh; }
    u32   index_count() const { return m_indexCount; }

    gl::Texture*      texture = nullptr;
    ParticleBlendMode blend = ParticleBlendMode::Additive;

protected:
    void _update(float dt) override;
    void _release_gpu() override { m_mesh.release_gpu(); }

private:
    void resetTrail(Chain& ch);

    static constexpr int MAX_CHAINS = 4;
    Chain  m_chains[MAX_CHAINS];
    int    m_activeChains = 0;

    // ring buffer: one flat array shared by all chains
    std::vector<Element> m_elements; // size = maxChains * maxElements
    int m_maxChains;
    int m_maxElementsPerChain;
    float m_elemLength = 0.025f; // trailLength / maxElements
    float m_trailLength = 1.2f;

    Mesh  m_mesh;
    bool  m_gpu_ready = false;
    u32   m_indexCount = 0;

    // scratch reused across rebuild() calls
    std::vector<MeshVertex> m_scratchVerts;
};
