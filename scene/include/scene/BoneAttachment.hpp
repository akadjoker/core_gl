#pragma once

#include "scene/Node3D.hpp"
#include <string>

class SkinnedMeshInstance;

// Ogre's TagPoint: a Node3D that follows one bone of a SkinnedMeshInstance.
// Each frame (after the instance animated) it copies the bone's global pose
// into its own transform; children — a sword, a lantern — ride the scene
// graph normally from there. This is the ONLY bone-node in the engine:
// bones themselves are flat arrays, you pay one node per attachment you
// actually use, never per bone.
//
//   auto* hand = sinbad->create_child<BoneAttachment>("hand");
//   hand->attach(sinbad, "Hand.R");
//   sword->set_parent(hand);
class BoneAttachment : public Node3D
{
public:
    static constexpr NodeType ClassType = NT_BONEATTACHMENT;

    explicit BoneAttachment(const std::string& name = "attachment");

    bool is_a(NodeType t) const override { return t == NT_BONEATTACHMENT || Node3D::is_a(t); }

    // target instance + bone by name (resolved once, index cached).
    // The attachment does NOT need to be a child of `instance`, but must
    // update after it — being its child guarantees that for free.
    bool attach(SkinnedMeshInstance* instance, const char* boneName);

    // extra offset on top of the bone pose (grip correction)
    void set_offset(const Vec3& pos, const Quaternion& rot);

    void _update(float dt) override;

private:
    SkinnedMeshInstance* m_target = nullptr;
    int m_bone = -1;
    Vec3 m_offsetPos = Vec3(0.f, 0.f, 0.f);
    Quaternion m_offsetRot;
};
