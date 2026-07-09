#pragma once

#include "scene/Node3D.hpp"
#include "scene/Mesh.hpp"
#include <coregl/gl_framebuffer.hpp>
#include <coregl/gl_renderbuffer.hpp>
#include <coregl/gl_texture.hpp>

// A planar water surface. Placing one in the tree is all game code does —
// the SceneRenderer detects it and renders two extra views of the scene
// before the main pass:
//   reflection — the camera mirrored across the water plane, clipped to
//                what is ABOVE the surface
//   refraction — the normal camera, clipped to what is BELOW the surface
// The water material then combines both textures with animated distortion
// and a fresnel term. The node itself never draws anything.
class WaterNode : public Node3D
{
public:
    static constexpr NodeType ClassType = NT_WATER;

    explicit WaterNode(const std::string& name = "Water") : Node3D(name) { m_type = NT_WATER; }
    ~WaterNode() override { release_gpu(); }

    bool is_a(NodeType t) const override { return t == NT_WATER || Node3D::is_a(t); }

    // half-extent of the water quad in world units (call before first render)
    void set_size(float half_extent) { m_half = half_extent; }
    float get_size() const { return m_half; }

    // world-space height of the surface (the plane is horizontal)
    float surface_height() { return get_global_position().y; }

    // ── look ──
    Vec3 water_color = Vec3(0.10f, 0.30f, 0.35f); // tint mixed into the result
    float wave_speed = 0.03f;                     // distortion scroll speed
    float distortion = 0.012f;                    // distortion strength (UV units)
    float wave_tiling = 6.f;                      // distortion pattern repeats
    float color_mix = 0.18f;                      // 0 = pure refl/refr, 1 = flat tint

    float time() const { return m_time; }

    // ── renderer-side (game code never calls these) ──
    bool ensure_gpu(int reflection_w, int reflection_h); // lazily builds FBOs + quad
    void release_gpu();

    gl::FrameBuffer& reflection_fbo() { return m_reflFbo; }
    gl::FrameBuffer& refraction_fbo() { return m_refrFbo; }
    gl::Texture& reflection_tex() { return m_reflTex; }
    gl::Texture& refraction_tex() { return m_refrTex; }
    // scene depth of the refraction view — per-pixel water depth in the
    // shader softens the shoreline and deepens the color
    gl::Texture& refraction_depth_tex() { return m_refrDepthTex; }
    int target_width() const { return m_targetW; }
    int target_height() const { return m_targetH; }
    Mesh& quad() { return m_quad; }

protected:
    void _update(float dt) override { m_time += dt; }
    void _release_gpu() override { release_gpu(); } // Scene::release_gpu reaches here

private:
    float m_half = 50.f;
    float m_time = 0.f;

    bool m_gpu_ready = false;
    int m_targetW = 0, m_targetH = 0;
    gl::FrameBuffer m_reflFbo, m_refrFbo;
    gl::Texture m_reflTex, m_refrTex, m_refrDepthTex;
    gl::RenderBuffer m_reflDepth;
    Mesh m_quad;
};
