#include "scene/BoneAttachment.hpp"
#include "scene/SkinnedMeshInstance.hpp"

BoneAttachment::BoneAttachment(const std::string& name) : Node3D(name)
{
    m_type = NT_BONEATTACHMENT;
}

bool BoneAttachment::attach(SkinnedMeshInstance* instance, const char* boneName)
{
    m_target = instance;
    m_bone = instance ? instance->bone_global_index(boneName) : -1;
    return m_bone >= 0;
}

void BoneAttachment::set_offset(const Vec3& pos, const Quaternion& rot)
{
    m_offsetPos = pos;
    m_offsetRot = rot;
}

void BoneAttachment::_update(float dt)
{
    (void)dt;
    if (!m_target || m_bone < 0) return;
    // TODO(evening): decompose m_target->bone_globals()[m_bone] (+offset)
    // into this node's local transform (relative to our parent)
}
