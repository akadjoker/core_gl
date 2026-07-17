#include "scene/FollowBehavior.hpp"
#include "scene/Node3D.hpp"

FollowBehavior::FollowBehavior(const std::string& name) : Behavior(name) {}

void FollowBehavior::on_process(Node3D& target, float dt)
{
    if (!m_follow) return;
    Vec3 desired = m_follow->get_global_position() + m_offset;
    Vec3 cur = target.get_position();
    if (m_smooth > 0.0f)
    {
        float a = Clamp(m_smooth * dt, 0.0f, 1.0f); // simple exponential lerp
        cur = cur + (desired - cur) * a;
    }
    else
        cur = desired;
    target.set_position(cur);
    if (m_lookAt) target.look_at(m_follow->get_global_position());
}
