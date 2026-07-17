#pragma once

#include "scene/Behavior.hpp"
#include "scene/Math.hpp"

// Oscillates the target along an axis (sinusoidal motion around its
// starting position). Useful for floating objects, platforms, pickups.
// Ported from tmp/core/include/OscillateBehavior.hpp.
class OscillateBehavior : public Behavior
{
public:
    explicit OscillateBehavior(const std::string& name = "Oscillate");

    void set_axis(const Vec3& axis) { m_axis = axis; }
    void set_amplitude(float a) { m_amplitude = a; }
    void set_frequency(float hz) { m_frequency = hz; } // cycles per second

protected:
    void _ready() override;
    void on_process(Node3D& target, float dt) override;

private:
    Vec3 m_axis{0, 1, 0};
    float m_amplitude = 1.0f, m_frequency = 0.5f;
    Vec3 m_start{0, 0, 0};
    float m_t = 0.0f;
    bool m_haveStart = false;
};
