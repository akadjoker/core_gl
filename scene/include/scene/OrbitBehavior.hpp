#pragma once

#include "scene/Behavior.hpp"
#include "scene/Math.hpp"

// Camera ORBITING a pivot point (model viewer):
//   right btn drag -> yaw/pitch    wheel -> zoom (distance)
// Writes position + look_at(pivot) on the target (1st Node3D ancestor).
// Ported from tmp/core/include/OrbitBehavior.hpp.
class OrbitBehavior : public Behavior
{
public:
    explicit OrbitBehavior(const std::string& name = "Orbit");

    void set_pivot(const Vec3& p) { m_pivot = p; }
    void set_distance(float d) { m_distance = d; }
    void set_angles(float yaw_deg, float pitch_deg) { m_yaw = yaw_deg; m_pitch = pitch_deg; }
    void set_sensitivity(float s) { m_sensitivity = s; }
    void set_zoom_speed(float z) { m_zoom = z; }
    void set_distance_limits(float mn, float mx) { m_minDist = mn; m_maxDist = mx; }

protected:
    void _ready() override;
    void on_process(Node3D& target, float dt) override;

private:
    Vec3 m_pivot{0, 0, 0};
    float m_distance = 10.0f;
    float m_yaw = 0.0f, m_pitch = 20.0f; // degrees
    float m_sensitivity = 0.25f, m_zoom = 1.5f;
    float m_minDist = 1.0f, m_maxDist = 500.0f;
};
