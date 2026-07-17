#include "scene/RotatorBehavior.hpp"
#include "scene/Node3D.hpp"

RotatorBehavior::RotatorBehavior(const std::string& name) : Behavior(name) {}

void RotatorBehavior::on_process(Node3D& target, float dt)
{
    target.rotate_axis(m_axis, ToRadians(m_speed * dt));
}
