#pragma once

#include "scene/Behavior.hpp"
#include "scene/Math.hpp"

// Continuously rotates the target around a local axis (degrees/second).
// Useful for showcase objects, platforms, etc.
// Ported from tmp/core/include/RotatorBehavior.hpp.
class RotatorBehavior : public Behavior
{
public:
    explicit RotatorBehavior(const std::string& name = "Rotator");

    void set_axis(const Vec3& axis) { m_axis = axis.normalized(); }
    void set_speed(float deg_per_sec) { m_speed = deg_per_sec; }

protected:
    void on_process(Node3D& target, float dt) override;

private:
    Vec3 m_axis{0, 1, 0};
    float m_speed = 45.0f;
};
