#pragma once

#include "scene/Behavior.hpp"

// Editor-style free-fly camera (controller = a child node of the camera):
//   WASD       move along orientation      Q/E    down/up
//   Shift      sprint                      wheel  adjusts speed
//   right btn  look (yaw/pitch)
// Simple setters/getters — editor widgets call these directly.
// Ported from tmp/core/include/FreeFlyBehavior.hpp.
class FreeFlyBehavior : public Behavior
{
public:
    explicit FreeFlyBehavior(const std::string& name = "FreeFly");

    void set_move_speed(float s) { m_move_speed = s; }
    float get_move_speed() const { return m_move_speed; }
    void set_sprint_multiplier(float m) { m_sprint = m; }
    float get_sprint_multiplier() const { return m_sprint; }
    void set_mouse_sensitivity(float s) { m_sensitivity = s; }
    float get_mouse_sensitivity() const { return m_sensitivity; }

protected:
    void _ready() override; // seeds yaw/pitch from the target
    void on_process(Node3D& target, float dt) override;

private:
    float m_move_speed = 8.0f;
    float m_sprint = 3.0f;
    float m_sensitivity = 0.15f;
    float m_yaw = 0.0f;   // degrees
    float m_pitch = 0.0f; // degrees
};
