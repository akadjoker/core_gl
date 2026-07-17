#pragma once

#include "scene/Behavior.hpp"
#include "scene/Math.hpp"

class CollisionSystem;

// Character controller (ported from tmp/core's CharacterCameraController):
// body with gravity + jump, W/S walk, A/D turn (yaw), left mouse = mouse-look.
// Collision is optional (CollisionSystem); without it, it falls onto a plane
// at groundY.
//   third_person_distance == 0 -> first person (eye on the body)
//   third_person_distance  > 0 -> camera behind the body (boom) = third person
// Ported from tmp/core/include/CharacterBehavior.hpp.
class CharacterBehavior : public Behavior
{
public:
    explicit CharacterBehavior(const std::string& name = "Character");

    void set_collision(CollisionSystem* c) { m_collision = c; }
    void set_body_position(const Vec3& p) { m_body = p; m_seeded = true; }
    Vec3 get_body_position() const { return m_body; }
    void set_radius(const Vec3& r) { m_radius = r; }
    void set_eye_offset(float y) { m_eyeOffset = y; }
    void set_ground_y(float y) { m_groundY = y; }
    void set_third_person(float boom) { m_boom = boom; }
    void set_speeds(float fwd, float back) { m_forwardSpeed = fwd; m_backwardSpeed = back; }
    void set_mouse_look(bool on) { m_mouseLook = on; }

protected:
    void _ready() override;
    void on_process(Node3D& target, float dt) override;

private:
    CollisionSystem* m_collision = nullptr;
    Vec3 m_body{0, 0, 0};
    Vec3 m_radius{0.4f, 0.9f, 0.4f};
    float m_eyeOffset = 1.7f;
    float m_groundY = 0.0f;
    float m_boom = 0.0f; // 0 = 1st person

    float m_yaw = 0.0f, m_pitch = 0.0f; // degrees
    float m_turnSpeed = 140.0f, m_sensitivity = 0.15f;
    float m_forwardSpeed = 6.0f, m_backwardSpeed = 4.5f, m_sprint = 2.0f;
    float m_gravity = 20.0f, m_jumpSpeed = 7.0f, m_groundSnap = 0.5f;
    float m_vSpeed = 0.0f;
    bool m_grounded = false, m_mouseLook = true, m_seeded = false;
};
