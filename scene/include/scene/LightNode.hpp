#pragma once

#include "scene/Node3D.hpp"
#include <coregl/gl_framebuffer.hpp>
#include <coregl/gl_texture.hpp>

enum class LightType
{
    Point,
    Spot
};

// A local light in the tree. Point lights radiate from their global
// position; spot lights shine along the node's forward axis (-Z), so aim
// them with look_at()/set_euler like any other node.
//
// With cast_shadows on, the SceneRenderer gives the light its own shadow
// map before the forward pass: a distance cubemap for point lights (six
// depth views), a projective 2D map for spots. The renderer supports up to
// 4 point + 4 spot lights per frame, of which 2 of each may cast shadows.
class LightNode : public Node3D
{
public:
    static constexpr NodeType ClassType = NT_LIGHT;

    explicit LightNode(const std::string& name = "Light") : Node3D(name) { m_type = NT_LIGHT; }
    ~LightNode() override { release_gpu(); }

    bool is_a(NodeType t) const override { return t == NT_LIGHT || Node3D::is_a(t); }

    LightType light_type = LightType::Point;
    Vec3 color = Vec3(1.f, 1.f, 1.f);
    float intensity = 1.f;
    float range = 15.f; // world units; no light beyond this distance

    // spot cone, radians (half-angles); intensity falls from inner to outer
    float inner_angle = 0.35f;
    float outer_angle = 0.55f;

    bool cast_shadows = false;
    int shadow_resolution = 512;

    // world-space direction the spot shines toward (node forward, -Z)
    Vec3 direction() { return rotateVec(get_global_rotation(), Vec3(0.f, 0.f, -1.f)); }

    // ── renderer-side ──
    bool ensure_gpu(); // builds the shadow map lazily (needs live context)
    void release_gpu();
    gl::Texture& shadow_tex() { return m_shadowTex; }
    gl::FrameBuffer& shadow_fbo() { return m_shadowFbo; }

protected:
    void _release_gpu() override { release_gpu(); }

private:
    static Vec3 rotateVec(const Quaternion& q, const Vec3& v) { return Mat4(q) * v; }

    bool m_gpu_ready = false;
    gl::Texture m_shadowTex; // cube (point) or 2D (spot) depth map
    gl::FrameBuffer m_shadowFbo;
};
