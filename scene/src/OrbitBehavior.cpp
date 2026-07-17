#include "scene/OrbitBehavior.hpp"
#include "scene/Input.hpp"
#include "scene/Node3D.hpp"
#include <cmath>

OrbitBehavior::OrbitBehavior(const std::string& name) : Behavior(name) {}

void OrbitBehavior::_ready()
{
    if (Node3D* t = get_target())
    {
        Vec3 p = t->get_global_position();
        Vec3 d = p - m_pivot;
        float r = d.length();
        if (r > 1e-3f) m_distance = r;
    }
}

void OrbitBehavior::on_process(Node3D& target, float dt)
{
    (void)dt;
    if (Input::IsMouseDown(RIGHT))
    {
        Vec2 d = Input::GetMouseDelta();
        m_yaw -= d.x * m_sensitivity;
        m_pitch = Clamp(m_pitch - d.y * m_sensitivity, -89.0f, 89.0f);
    }
    float wheel = Input::GetMouseWheelMoveV();
    if (wheel != 0.0f) m_distance = Clamp(m_distance - wheel * m_zoom, m_minDist, m_maxDist);

    float yr = ToRadians(m_yaw), pr = ToRadians(m_pitch);
    float cp = cosf(pr);
    Vec3 pos = m_pivot + Vec3(m_distance * cp * sinf(yr), m_distance * sinf(pr), m_distance * cp * cosf(yr));
    target.set_position(pos);
    target.look_at(m_pivot);
}
