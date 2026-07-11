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

// copies the bone's evaluated global (instance-model space) into this
// node's local transform. As a CHILD of the instance that puts children
// exactly on the bone; the offset corrects the grip.
void BoneAttachment::_update(float dt)
{
    (void)dt;
    if (!m_target || m_bone < 0 || m_bone >= (int)m_target->bone_globals().size()) return;

    Mat4 boneWorld = m_target->bone_globals()[(size_t)m_bone] *
                     (Mat4::Translate(m_offsetPos) * Mat4::Rotate(m_offsetRot));
    Vec3 t, r, s;
    boneWorld.decompose(t, r, s);
    set_position(t);
    set_euler(r);
    set_scale(s);
}
