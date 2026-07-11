#pragma once

#include "scene/Node3D.hpp"
 
enum class Projection
{
    Perspective,
    Orthographic
};

class Camera3D : public Node3D
{
public:
    static constexpr NodeType ClassType = NT_CAMERA3D;

    explicit Camera3D(const std::string& name = "Camera3D");

    bool is_a(NodeType t) const override { return t == NT_CAMERA3D || Node3D::is_a(t); }

    // ── lens ──
    void set_perspective(float fov_degrees, float near_plane, float far_plane);
    void set_orthographic(float size, float near_plane, float far_plane);

    void set_fov(float degrees); // vertical FOV, perspective
    float get_fov() const { return m_fov; }
    void set_clip(float near_plane, float far_plane);
    float get_near() const { return m_near; }
    float get_far() const { return m_far; }
    void set_ortho_size(float half_height);
    float get_ortho_size() const { return m_ortho_size; }

    Projection get_projection_type() const { return m_projection; }

    // aspect ratio: set from the viewport size (w/h)
    void set_aspect(float aspect);
    void set_viewport_size(int width, int height);
    float get_aspect() const { return m_aspect; }

    // ── matrices ──
    const Mat4& get_projection_matrix(); // rebuilt if the lens changed
    Mat4 get_view_matrix();              // rigid inverse of the world transform
    Mat4 get_view_projection();

    // ── picking ──
    // world-space ray direction through pixel (px,py) of a viewport_w x
    // viewport_h viewport — mouse picking is `pick(cam->get_position(),
    // cam->screen_to_ray(mx, my, w, h), ...)`
    Vec3 screen_to_ray(float px, float py, int viewport_w, int viewport_h);
    // projects a world point to pixel coordinates; false when behind the
    // camera (out params untouched)
    bool world_to_screen(const Vec3& world, int viewport_w, int viewport_h, float& sx,
                         float& sy);

private:
    Projection m_projection;
    float m_fov; // degrees, vertical (perspective)
    float m_near;
    float m_far;
    float m_ortho_size; // half-height in world units (orthographic)
    float m_aspect;

    Mat4 m_proj;
    bool m_proj_dirty;
};
