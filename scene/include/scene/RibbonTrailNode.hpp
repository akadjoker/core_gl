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
// records a history of world positions with age. On rebuild (called by
// the renderer once the camera basis is known), a strip of camera-facing
// quads is baked into a dynamic Mesh with color riding in the tangent
// slot — the same vertex layout as particles, so it reuses the particle
// shader directly.  Drawn in the transparent pass with depth-write off.
class RibbonTrailNode : public Node3D
{
public:
    static constexpr NodeType ClassType = NT_RIBBONTRAIL;

    struct Element
    {
        Vec3 position{0, 0, 0};
        float age = 0.f;
    };

    struct Chain
    {
        Node3D* emitter = nullptr;
        std::vector<Element> elements;
        Vec4 startColor{1, 1, 1, 1};
        Vec4 endColor{1, 1, 1, 0};
        float startWidth = 0.7f;
        float endWidth = 0.05f;
        bool active = true;
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
    void setTrailLength(float seconds) { m_trailLength = seconds > 0.01f ? seconds : 0.01f; }
    void setMinSegment(float v) { m_minSeg = v > 0.001f ? v : 0.001f; }

    // ── renderer-side (game code never calls these) ──
    bool ensure_gpu();                                // lazily builds the dynamic Mesh
    void rebuild(const Vec3& camPos, const Vec3& camUp);
    Mesh& quad_mesh() { return m_mesh; }
    u32   index_count() const { return m_indexCount; }

    gl::Texture*      texture = nullptr; // non-owning (AssetManager)
    ParticleBlendMode blend = ParticleBlendMode::Additive;

protected:
    void _update(float dt) override;
    void _release_gpu() override { m_mesh.release_gpu(); }

private:
    std::vector<Chain> m_chains;
    Mesh  m_mesh;
    bool  m_gpu_ready = false;
    int   m_maxChains;
    int   m_maxElements;
    u32   m_indexCount = 0;
    float m_trailLength = 1.2f;
    float m_minSeg = 0.05f;
    float m_time = 0.f; // accumulated simulation time
};
