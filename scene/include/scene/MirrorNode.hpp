#pragma once

#include "scene/Node3D.hpp"
#include "scene/Mesh.hpp"
#include <coregl/gl_framebuffer.hpp>
#include <coregl/gl_renderbuffer.hpp>
#include <coregl/gl_texture.hpp>

// A still, reflection-only planar surface — a real mirror, a polished floor,
// a puddle, placed anywhere in the world at any orientation. The reflection
// -only half of what WaterNode does: SceneRenderer detects it and renders
// one extra view of the scene before the main pass, with the camera
// reflected across the mirror's own plane (position + local +Y = the
// plane's point + normal), clipped to what is in front of the surface. No
// refraction, no wave distortion — a flat mirror, not water.
class MirrorNode : public Node3D
{
public:
    static constexpr NodeType ClassType = NT_MIRROR;

    explicit MirrorNode(const std::string& name = "Mirror") : Node3D(name) { m_type = NT_MIRROR; }
    ~MirrorNode() override { release_gpu(); }

    bool is_a(NodeType t) const override { return t == NT_MIRROR || Node3D::is_a(t); }

    // half-extent of the mirror quad in world units (call before first render)
    void set_size(float half_extent) { m_half = half_extent; }
    float get_size() const { return m_half; }

    // world-space point on the mirror plane and its outward normal (the
    // reflective face points along local +Y — rotate the node to orient it,
    // e.g. a wall mirror needs a 90 degree tilt so +Y points into the room)
    Vec3 plane_point() { return get_global_position(); }
    Vec3 plane_normal() { return (Mat4(get_global_rotation()) * Vec3(0.f, 1.f, 0.f)).normalized(); }

    // ── look ──
    Vec3 tint = Vec3(0.05f, 0.05f, 0.06f); // color shown at normal incidence
    // 0..1 floor on how much tint mixes in even at grazing angles; Fresnel
    // ramps from this up to 1.0 (pure reflection) as the view angle widens
    float reflectivity = 0.15f;

    // ── renderer-side (game code never calls these) ──
    bool ensure_gpu(int reflection_w, int reflection_h); // lazily builds FBO + quad
    void release_gpu();

    gl::FrameBuffer& reflection_fbo() { return m_reflFbo; }
    gl::Texture& reflection_tex() { return m_reflTex; }
    int target_width() const { return m_targetW; }
    int target_height() const { return m_targetH; }
    Mesh& quad() { return m_quad; }

protected:
    void _release_gpu() override { release_gpu(); } // Scene::release_gpu reaches here

private:
    void build_surface(Mesh& mesh);

    float m_half = 50.f;

    bool m_gpu_ready = false;
    int m_targetW = 0, m_targetH = 0;
    gl::FrameBuffer m_reflFbo;
    gl::Texture m_reflTex;
    gl::RenderBuffer m_reflDepth;
    Mesh m_quad;
};
