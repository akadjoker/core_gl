#pragma once

#include "scene/WaterNode.hpp"

// Deep-water surface driven by the Gerstner-wave ocean shader in
// assets/shaders/water.ps|.fs: four waves displace a dense grid in the
// vertex stage, the fragment stage adds bump distortion over the same
// reflection/refraction views a WaterNode gets, plus shore/crest foam.
//
// It IS a WaterNode as far as the renderer's views are concerned — only
// the surface pass differs. Needs two tiling textures (non-owning, from
// the AssetManager): a bump map (rg perturbs the reflections) and a foam
// pattern.
class OceanNode : public WaterNode
{
public:
    static constexpr NodeType ClassType = NT_OCEAN;

    explicit OceanNode(const std::string& name = "Ocean") : WaterNode(name) { m_type = NT_OCEAN; }

    bool is_a(NodeType t) const override { return t == NT_OCEAN || WaterNode::is_a(t); }

    // ── Gerstner waves: (direction x, direction y, steepness, wavelength) ──
    // NOTE: wave_height doubles as the reflection/refraction distortion
    // strength in the shader — keep it small (~0.05) and let the steepness
    // values carry the wave size.
    Vec4 wave1 = Vec4(1.0f, 0.3f, 0.90f, 80.f);
    Vec4 wave2 = Vec4(0.7f, -0.6f, 0.70f, 37.f);
    Vec4 wave3 = Vec4(-0.4f, 0.8f, 0.55f, 22.f);
    Vec4 wave4 = Vec4(0.2f, 0.9f, 0.40f, 12.f);
    float wave_height = 0.06f; // scales displacement AND uv distortion

    // ── surface look ──
    Vec4 ocean_color = Vec4(0.02f, 0.22f, 0.30f, 1.f);
    float color_blend = 0.25f;   // 0 = pure refl/refr, 1 = flat ocean_color
    float bump_strength = 0.02f; // reflection/refraction distortion
    float bump_uv_scale = 12.f;  // his u_waveLength: bump uv divisor
    float wind_force = 0.02f;    // bump scroll speed
    Vec2 wind_dir = Vec2(1.f, 0.4f);
    float depth_scale = 25.f; // his `mult`: depth normalization distance

    // ── foam ──
    float shore_fade = 1.6f;     // soft-edge fade distance at the waterline
    float foam_range = 5.f;      // shoreline foam reach (world units)
    float foam_scale = 0.05f;    // foam texture tiling over world XZ
    float foam_speed = 0.04f;    // foam scroll
    float foam_intensity = 0.7f; // 0..1

    gl::Texture* bump = nullptr; // non-owning (AssetManager); if null, uses embedded
    gl::Texture* foam = nullptr;

    int grid_resolution = 200; // cells per side of the displaceable grid

    // generates procedural bump + foam textures if external ones aren't set;
    // called lazily by the renderer before drawing. Safe to call repeatedly.
    void ensure_textures();
    void release_textures();

    gl::Texture& builtin_bump() { return m_builtinBump; }
    gl::Texture& builtin_foam() { return m_builtinFoam; }

protected:
    void build_surface(Mesh& mesh) override;
    void _release_gpu() override { release_textures(); }

private:
    gl::Texture m_builtinBump;  // procedural normal map (rg perturbation)
    gl::Texture m_builtinFoam;  // procedural white-noise foam pattern (R8)
    bool m_texturesReady = false;
};
