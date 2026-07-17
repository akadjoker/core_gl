#include "scene/CharacterBehavior.hpp"
#include "scene/Collision.hpp"
#include "scene/Input.hpp"
#include "scene/Node3D.hpp"
#include <cmath>

CharacterBehavior::CharacterBehavior(const std::string& name) : Behavior(name) {}

void CharacterBehavior::_ready()
{
    if (Node3D* t = get_target())
    {
        Vec3 e = t->get_euler();
        m_pitch = ToDegrees(e.x);
        m_yaw = ToDegrees(e.y);
        if (!m_seeded) m_body = t->get_global_position() - Vec3(0, m_eyeOffset, 0);
        m_seeded = true;
    }
}

void CharacterBehavior::on_process(Node3D& target, float dt)
{
    float movement = 0.0f, turn = 0.0f;
    if (Input::IsKeyDown(KEY_W) || Input::IsKeyDown(KEY_UP)) movement = m_forwardSpeed;
    if (Input::IsKeyDown(KEY_S) || Input::IsKeyDown(KEY_DOWN)) movement = -m_backwardSpeed;
    if (Input::IsKeyDown(KEY_A) || Input::IsKeyDown(KEY_LEFT)) turn = -m_turnSpeed;
    if (Input::IsKeyDown(KEY_D) || Input::IsKeyDown(KEY_RIGHT)) turn = m_turnSpeed;
    if (Input::IsKeyDown(KEY_LEFT_SHIFT) && movement > 0.0f) movement *= m_sprint;

    m_yaw += turn * dt;
    if (m_mouseLook && Input::IsMouseDown(LEFT))
    {
        Vec2 d = Input::GetMouseDelta();
        m_yaw -= d.x * m_sensitivity;
        m_pitch = Clamp(m_pitch - d.y * m_sensitivity, -89.0f, 89.0f);
    }
    if (Input::IsKeyPressed(KEY_SPACE) && m_grounded) m_vSpeed = m_jumpSpeed;

    float yr = ToRadians(m_yaw);
    Vec3 flatForward(sinf(yr), 0.0f, -cosf(yr));

    if (m_collision)
    {
        Vec3 moveDelta = flatForward * (movement * dt);
        m_body = m_collision->collideAndSlide(m_body, moveDelta, m_radius);
        m_vSpeed -= m_gravity * dt;
        Vec3 cand = m_body;
        cand.y += m_vSpeed * dt;
        bool groundedNow = false;
        CollisionInfo hit;
        Vec3 rayO = cand;
        rayO.y += m_radius.y + m_groundSnap;
        float rayLen = (m_radius.y + m_groundSnap) * 2.0f + 4.0f;
        if (m_vSpeed <= 0.0f && m_collision->rayCast(Ray(rayO, Vec3(0, -1, 0)), rayLen, hit))
        {
            float targetY = hit.point.y + m_radius.y;
            if (cand.y <= targetY + m_groundSnap)
            {
                cand.y = targetY;
                m_vSpeed = 0.0f;
                groundedNow = true;
            }
        }
        m_body = cand;
        m_grounded = groundedNow;
    }
    else
    {
        m_body += flatForward * (movement * dt);
        m_vSpeed -= m_gravity * dt;
        m_body.y += m_vSpeed * dt;
        if (m_body.y <= m_groundY)
        {
            m_body.y = m_groundY;
            m_vSpeed = 0.0f;
            m_grounded = true;
        }
        else
            m_grounded = false;
    }

    Vec3 eye = m_body + Vec3(0, m_eyeOffset, 0);
    if (m_boom > 0.0f) // third person: camera behind the eye along the view
    {
        float pr = ToRadians(m_pitch);
        Vec3 fwd(cosf(pr) * sinf(yr), sinf(pr), -cosf(pr) * cosf(yr));
        target.set_position(eye - fwd * m_boom);
        target.look_at(eye);
    }
    else // first person
    {
        target.set_position(eye);
        target.set_euler(Vec3(ToRadians(m_pitch), yr, 0.0f));
    }
}
