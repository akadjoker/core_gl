#include "scene/FreeFlyBehavior.hpp"
#include "scene/Input.hpp"
#include "scene/Node3D.hpp"

FreeFlyBehavior::FreeFlyBehavior(const std::string& name) : Behavior(name) {}

void FreeFlyBehavior::_ready()
{
    // Starts from the camera's current orientation (no jump on first look).
    if (Node3D* t = get_target())
    {
        Vec3 e = t->get_euler();
        m_pitch = ToDegrees(e.x);
        m_yaw = ToDegrees(e.y);
    }
}

void FreeFlyBehavior::on_process(Node3D& target, float dt)
{
    // ── Look: hold the right mouse button ──
    if (Input::IsMouseDown(RIGHT))
    {
        Vec2 d = Input::GetMouseDelta();
        m_yaw -= d.x * m_sensitivity;
        m_pitch -= d.y * m_sensitivity;
        m_pitch = Clamp(m_pitch, -89.0f, 89.0f);
        // YXZ: yaw around Y, then local pitch around X; no roll.
        target.set_euler(Vec3(ToRadians(m_pitch), ToRadians(m_yaw), 0.0f));
    }

    // ── Mouse wheel adjusts fly speed ──
    float wheel = Input::GetMouseWheelMoveV();
    if (wheel != 0.0f) m_move_speed = Clamp(m_move_speed + wheel, 0.5f, 200.0f);

    // ── Movement along the orientation ──
    float speed = m_move_speed;
    if (Input::IsKeyDown(KEY_LEFT_SHIFT)) speed *= m_sprint;
    float v = speed * dt;

    if (Input::IsKeyDown(KEY_W)) target.advance(v);
    if (Input::IsKeyDown(KEY_S)) target.advance(-v);
    if (Input::IsKeyDown(KEY_D)) target.strafe(v);
    if (Input::IsKeyDown(KEY_A)) target.strafe(-v);
    if (Input::IsKeyDown(KEY_E)) target.lift(v);
    if (Input::IsKeyDown(KEY_Q)) target.lift(-v);
}
