#pragma once

#include "scene/Node3D.hpp"
#include <string>

class MorphMeshInstance;

// Node3D that follows one MD3 tag (a named per-frame attachment point) of
// a MorphMeshInstance. Same shape as BoneAttachment, just reading a morph
// tag transform instead of a skeleton bone — used to glue separate MD3
// parts together (lower -> tag_torso -> upper -> tag_head -> head, or
// tag_weapon -> a weapon model) or to hang props off a Quake-style rig.
//
//   auto* torso = lower->create_child<TagAttachment>("torso_attach");
//   torso->attach(lower, "tag_torso");
//   upper->set_parent(torso);
class TagAttachment : public Node3D
{
public:
    static constexpr NodeType ClassType = NT_TAGATTACHMENT;

    explicit TagAttachment(const std::string& name = "tag_attachment");

    bool is_a(NodeType t) const override { return t == NT_TAGATTACHMENT || Node3D::is_a(t); }

    // target instance + tag by name (resolved once, index cached via the
    // instance's MorphTags — re-attach if the instance's data changes).
    bool attach(MorphMeshInstance* instance, const char* tagName);

    void _update(float dt) override;

private:
    MorphMeshInstance* m_target = nullptr;
    std::string m_tagName;
};
