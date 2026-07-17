#include "scene/MayaBehavior.hpp"
#include "scene/Input.hpp"
#include "scene/Node3D.hpp"
#include <cmath>

MayaBehavior::MayaBehavior(const std::string& name) : Behavior(name) {}

void MayaBehavior::_ready()
{
    if (Node3D* t = get_target())
    {
        Vec3 dir = t->get_global_position() - m_target;
        float d = dir.length();
        if (d > 1e-3f)
        {
            dir = dir * (1.0f / d);
            m_distance = d;
            m_pitch = ToDegrees(asinf(Clamp(dir.y, -1.0f, 1.0f)));
            m_yaw = ToDegrees(atan2f(dir.x, dir.z));
        }
    }
}

void MayaBehavior::on_process(Node3D& target, float dt)
{
    (void)dt;
    bool alt = Input::IsKeyDown(KEY_LEFT_ALT);

    if (alt && Input::IsMouseDown(LEFT)) // orbit
    {
        Vec2 d = Input::GetMouseDelta();
        m_yaw += d.x * m_orbitSpeed;
        m_pitch = Clamp(m_pitch - d.y * m_orbitSpeed, -89.0f, 89.0f);
    }

    float yr = ToRadians(m_yaw), pr = ToRadians(m_pitch);
    float cp = cosf(pr);
    Vec3 fwd(cp * sinf(yr), sinf(pr), cp * cosf(yr)); // from target to camera

    if (alt && Input::IsMouseDown(MIDDLE)) // pan
    {
        Vec2 d = Input::GetMouseDelta();
        Vec3 right = Vec3::Cross(Vec3(0, 1, 0), fwd).normalized();
        Vec3 up = Vec3::Cross(fwd, right).normalized();
        float pan = m_panSpeed * m_distance;
        m_target = m_target - right * (d.x * pan) + up * (d.y * pan);
    }

    float scroll = Input::GetMouseWheelMoveV();
    if (alt && Input::IsMouseDown(RIGHT)) scroll -= Input::GetMouseDelta().x * 0.05f;
    m_distance = Clamp(m_distance - scroll * m_zoomSpeed, m_minDist, m_maxDist);

    target.set_position(m_target + fwd * m_distance);
    target.look_at(m_target);
}
