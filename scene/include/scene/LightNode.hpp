#pragma once

#include "scene/Node3D.hpp"
#include <coregl/gl_framebuffer.hpp>
#include <coregl/gl_texture.hpp>

// Base of every local light: what it emits and whether it casts shadows.
// Concrete lights are their own node types with their own values —
// PointLight (radius) and SpotLight (cone + direction). The renderer
// supports up to 4 of each per frame, of which 2 of each may cast shadows.
class LightNode : public Node3D
{
public:
    static constexpr NodeType ClassType = NT_LIGHT;

    explicit LightNode(const std::string& name = "Light") : Node3D(name) { m_type = NT_LIGHT; }
    ~LightNode() override { release_gpu(); }

    bool is_a(NodeType t) const override { return t == NT_LIGHT || Node3D::is_a(t); }

    Vec3 color = Vec3(1.f, 1.f, 1.f);
    float intensity = 1.f;
    bool cast_shadows = false;
    int shadow_resolution = 512;

    // ── renderer-side ──
    virtual bool ensure_gpu() = 0; // builds the shadow map lazily
    void release_gpu();
    gl::Texture& shadow_tex() { return m_shadowTex; }
    gl::FrameBuffer& shadow_fbo() { return m_shadowFbo; }

protected:
    void _release_gpu() override { release_gpu(); } // Scene::release_gpu reaches here

    bool m_gpu_ready = false;
    gl::Texture m_shadowTex; // cube (point) or 2D (spot) depth map
    gl::FrameBuffer m_shadowFbo;
};

// Radiates from its global position, fading to nothing at `range`. Shadows
// come from a linear-distance cubemap (six depth views around the light).
class PointLight : public LightNode
{
public:
    static constexpr NodeType ClassType = NT_POINTLIGHT;

    explicit PointLight(const std::string& name = "PointLight") : LightNode(name)
    {
        m_type = NT_POINTLIGHT;
    }

    bool is_a(NodeType t) const override { return t == NT_POINTLIGHT || LightNode::is_a(t); }

    float range = 15.f; // world units; no light beyond this distance

    bool ensure_gpu() override;
};

// Shines a cone along the node's forward axis (-Z) — aim it with
// look_at()/set_euler like any other node. Intensity falls off between the
// inner and outer half-angles. Shadows come from one projective 2D map.
class SpotLight : public LightNode
{
public:
    static constexpr NodeType ClassType = NT_SPOTLIGHT;

    explicit SpotLight(const std::string& name = "SpotLight") : LightNode(name)
    {
        m_type = NT_SPOTLIGHT;
    }

    bool is_a(NodeType t) const override { return t == NT_SPOTLIGHT || LightNode::is_a(t); }

    float range = 25.f;
    float inner_angle = 0.35f; // radians, half-angle: full brightness inside
    float outer_angle = 0.55f; // radians, half-angle: dark outside

    // world-space direction the cone points toward (node forward, -Z)
    Vec3 direction() { return Mat4(get_global_rotation()) * Vec3(0.f, 0.f, -1.f); }

    bool ensure_gpu() override;
};
