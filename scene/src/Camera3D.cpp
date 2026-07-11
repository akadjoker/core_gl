#include "scene/Camera3D.hpp"

Camera3D::Camera3D(const std::string& name)
    : Node3D(name), m_projection(Projection::Perspective), m_fov(60.0f), m_near(0.1f),
      m_far(1000.0f), m_ortho_size(10.0f), m_aspect(16.0f / 9.0f), m_proj(), m_proj_dirty(true)
{
    m_type = NT_CAMERA3D;
}

void Camera3D::set_perspective(float fov_degrees, float near_plane, float far_plane)
{
    m_projection = Projection::Perspective;
    m_fov = fov_degrees;
    m_near = near_plane;
    m_far = far_plane;
    m_proj_dirty = true;
}

void Camera3D::set_orthographic(float size, float near_plane, float far_plane)
{
    m_projection = Projection::Orthographic;
    m_ortho_size = size;
    m_near = near_plane;
    m_far = far_plane;
    m_proj_dirty = true;
}

void Camera3D::set_fov(float degrees)
{
    m_fov = degrees;
    m_proj_dirty = true;
}

void Camera3D::set_clip(float near_plane, float far_plane)
{
    m_near = near_plane;
    m_far = far_plane;
    m_proj_dirty = true;
}

void Camera3D::set_ortho_size(float half_height)
{
    m_ortho_size = half_height;
    m_proj_dirty = true;
}

void Camera3D::set_aspect(float aspect)
{
    m_aspect = aspect;
    m_proj_dirty = true;
}

void Camera3D::set_viewport_size(int width, int height)
{
    if (height <= 0) return;
    set_aspect((float)width / (float)height);
}

const Mat4& Camera3D::get_projection_matrix()
{
    if (m_proj_dirty)
    {
        if (m_projection == Projection::Perspective)
        {
            m_proj =
                Mat4::Perspective((double)m_fov, (double)m_aspect, (double)m_near, (double)m_far);
        }
        else
        {
            const float h = m_ortho_size;
            const float w = m_ortho_size * m_aspect;
            m_proj = Mat4::Ortho(-w, w, -h, h, m_near, m_far);
        }
        m_proj_dirty = false;
    }
    return m_proj;
}

Mat4 Camera3D::get_view_matrix()
{
    // rigid inverse: for M = T(t)*R(q), M^-1 = R(q^-1) with translation
    // -R(q^-1)*t — avoids a general 4x4 inverse and ignores any scale
    Quaternion inv_rot = get_global_rotation().inverted();
    Vec3 t = get_global_position();

    Mat4 view = Mat4::Rotate(inv_rot);
    Vec3 nt = inv_rot.rotateVector(-t);
    view.c[3][0] = nt.x;
    view.c[3][1] = nt.y;
    view.c[3][2] = nt.z;
    return view;
}

Mat4 Camera3D::get_view_projection()
{
    return get_projection_matrix() * get_view_matrix();
}

// unproject the pixel at the near and far planes; the ray is the world-space
// line between them (works for perspective and orthographic)
Vec3 Camera3D::screen_to_ray(float px, float py, int viewport_w, int viewport_h)
{
    const float nx = 2.f * px / (float)viewport_w - 1.f;
    const float ny = 1.f - 2.f * py / (float)viewport_h; // pixel y grows down
    Mat4 inv = Mat4::Inverse(get_view_projection());

    Vec4 nearP = inv * Vec4(nx, ny, -1.f, 1.f);
    Vec4 farP = inv * Vec4(nx, ny, 1.f, 1.f);
    if (fabsf(nearP.w) > 1e-9f) nearP = nearP / nearP.w;
    if (fabsf(farP.w) > 1e-9f) farP = farP / farP.w;

    return Vec3(farP.x - nearP.x, farP.y - nearP.y, farP.z - nearP.z).normalized();
}

bool Camera3D::world_to_screen(const Vec3& world, int viewport_w, int viewport_h, float& sx,
                               float& sy)
{
    Vec4 clip = get_view_projection() * Vec4(world.x, world.y, world.z, 1.f);
    if (clip.w <= 1e-6f) return false; // behind the camera
    sx = (clip.x / clip.w * 0.5f + 0.5f) * (float)viewport_w;
    sy = (0.5f - clip.y / clip.w * 0.5f) * (float)viewport_h;
    return true;
}
