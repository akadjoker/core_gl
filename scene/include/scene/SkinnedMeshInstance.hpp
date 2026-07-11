#pragma once

#include "scene/Node3D.hpp"
#include "scene/AnimationPlayer.hpp"
#include "scene/Skeleton.hpp"
#include <vector>

class SkinnedMesh;
class Material;

// One animated character in the scene (Ogre's Entity): points at a shared
// SkinnedMesh and owns only its STATE — the animation player, the pose
// buffers and the skinning palette the renderer uploads as u_bones[].
// _update(dt) advances the animation and evaluates the skeleton, so bone
// attachments (children) read fresh globals in the same frame.
class SkinnedMeshInstance : public Node3D
{
public:
    static constexpr NodeType ClassType = NT_SKINNEDMESH;

    explicit SkinnedMeshInstance(const std::string& name = "skinned");

    bool is_a(NodeType t) const override { return t == NT_SKINNEDMESH || Node3D::is_a(t); }

    void set_mesh(SkinnedMesh* shared); // not owned; binds the player
    SkinnedMesh* get_mesh() const { return m_shared; }
    void set_material(Material* m) { m_material = m; }
    Material* get_material() const { return m_material; }

    AnimationPlayer& animation() { return m_player; }

    // evaluated this frame by _update
    const std::vector<Mat4>& palette() const { return m_palette; }        // u_bones[]
    const std::vector<Mat4>& bone_globals() const { return m_globals; }   // attachments
    int bone_global_index(const char* boneName) const;                    // -1 missing

    // freeze/resume the whole character (editor, off-screen throttling)
    void set_animation_enabled(bool on) { m_animate = on; }

    void _update(float dt) override;
    void _release_gpu() override;

private:
    SkinnedMesh* m_shared = nullptr;
    Material* m_material = nullptr;
    AnimationPlayer m_player;
    std::vector<LocalPose> m_locals;
    std::vector<Mat4> m_globals;
    std::vector<Mat4> m_palette;
    bool m_animate = true;
};
