#pragma once

#include "scene/Behavior.hpp"
#include "scene/Math.hpp"

// Maya-style camera (ported from tmp/core's MayaCameraController):
//   Alt+LMB  orbit      Alt+MMB  pan      wheel / Alt+RMB  zoom
// Writes position + look_at(target) on the target (1st Node3D ancestor).
// Ported from tmp/core/include/MayaBehavior.hpp.
class MayaBehavior : public Behavior
{
public:
    explicit MayaBehavior(const std::string& name = "Maya");

    void set_target_point(const Vec3& t) { m_target = t; }
    void set_distance(float d) { m_distance = d; }
    void set_distance_limits(float mn, float mx) { m_minDist = mn; m_maxDist = mx; }

protected:
    void _ready() override;
    void on_process(Node3D& target, float dt) override;

private:
    Vec3 m_target{0, 0, 0};
    float m_distance = 12.0f, m_minDist = 0.5f, m_maxDist = 1000.0f;
    float m_orbitSpeed = 0.4f, m_panSpeed = 0.05f, m_zoomSpeed = 1.0f;
    float m_yaw = 0.0f, m_pitch = 20.0f; // degrees
};
