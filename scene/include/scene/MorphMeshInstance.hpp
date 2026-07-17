#pragma once

#include "scene/Mesh.hpp" // MeshVertex
#include "scene/MeshInstance.hpp"
#include "scene/MorphAnimation.hpp"
#include <vector>

// A MeshInstance whose Mesh is a Quake-style vertex/morph animation
// (MD2/MD3): the vertex buffer is dynamic and gets re-blended on the CPU
// every frame instead of being static. Deliberately a MeshInstance
// subclass (not a new node type) — it inherits NT_MESHINSTANCE, so
// Scene::collect()/SceneRenderer draw it exactly like any other mesh, no
// renderer changes needed. The mesh's own vertex shader/material path is
// unchanged too (no skinning uniforms — the CPU blend already produced the
// final positions/normals).
//
//   auto* lower = scene.root().create_child<MorphMeshInstance>("lower");
//   Mesh* mesh = assets.createMesh("lower_mesh");
//   MorphKeyframes anim; std::vector<Material*> mats; MorphTags tags;
//   load_md3("assets/.../lower.md3", *mesh, anim, mats, &tags);
//   lower->set_anim_data(mesh, std::move(anim), std::move(tags));
//   lower->set_materials(mats);
//   lower->add_clip("walk", 0, 11, 15.f, true);
//   lower->play("walk");
class MorphMeshInstance : public MeshInstance
{
public:
    explicit MorphMeshInstance(const std::string& name = "MorphMeshInstance");

    // `mesh` must already be uploaded via upload_dynamic() (load_md2/
    // load_md3 do this) — non-owning, same convention as set_mesh().
    void set_anim_data(Mesh* mesh, MorphKeyframes keyframes, MorphTags tags = MorphTags());

    void add_clip(const std::string& name, int first, int last, float fps, bool loop = true)
    {
        m_animator.add_clip(name, first, last, fps, loop);
    }
    bool play(const std::string& name, float blendTime = 0.15f) { return m_animator.play(name, blendTime); }
    const std::string& current_clip() const { return m_animator.current_clip(); }

    // local transform (this instance's model space) of a tag by name.
    // Identity (and false) if the tag doesn't exist or this mesh has none.
    bool get_tag_transform(const char* tagName, Mat4& out) const;

    void _update(float dt) override;

private:
    MorphKeyframes m_keyframes;
    MorphTags m_tags;
    MorphAnimator m_animator;
    std::vector<MeshVertex> m_work; // scratch buffer re-blended each frame
};
