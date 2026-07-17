#include "scene/TagAttachment.hpp"
#include "scene/MorphMeshInstance.hpp"

TagAttachment::TagAttachment(const std::string& name) : Node3D(name)
{
    m_type = NT_TAGATTACHMENT;
}

bool TagAttachment::attach(MorphMeshInstance* instance, const char* tagName)
{
    m_target = instance;
    m_tagName = tagName ? tagName : "";
    Mat4 dummy;
    return instance && instance->get_tag_transform(m_tagName.c_str(), dummy);
}

// copies the tag's evaluated local (instance-model space) transform into
// this node's local transform. As a CHILD of the instance that puts
// children exactly on the tag, same convention as BoneAttachment.
void TagAttachment::_update(float dt)
{
    (void)dt;
    if (!m_target) return;

    Mat4 tagLocal;
    if (!m_target->get_tag_transform(m_tagName.c_str(), tagLocal)) return;

    Vec3 t, r, s;
    tagLocal.decompose(t, r, s);
    set_position(t);
    set_euler(r);
    set_scale(s);
}
