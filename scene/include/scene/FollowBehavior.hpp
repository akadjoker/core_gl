#pragma once

#include "scene/Behavior.hpp"
#include "scene/Math.hpp"

class Node3D;

// Makes the target FOLLOW another Node3D, at a (world) offset, with
// optional smoothing (lerp) and, optionally, looking at the followed node.
// Chase cam / companion.
// Ported from tmp/core/include/FollowBehavior.hpp.
class FollowBehavior : public Behavior
{
public:
    explicit FollowBehavior(const std::string& name = "Follow");

    void set_follow(Node3D* node) { m_follow = node; } // non-owning
    void set_offset(const Vec3& o) { m_offset = o; }
    void set_smooth(float s) { m_smooth = s; } // 0 = instant, >0 = lerp/sec
    void set_look_at(bool on) { m_lookAt = on; }

protected:
    void on_process(Node3D& target, float dt) override;

private:
    Node3D* m_follow = nullptr;
    Vec3 m_offset{0, 4, 10};
    float m_smooth = 6.0f;
    bool m_lookAt = true;
};
