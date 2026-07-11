#pragma once

#include "scene/Mesh.hpp"
#include "scene/Skeleton.hpp"
#include "scene/AnimationClip.hpp"
#include <memory>
#include <string>
#include <vector>

// The SHARED skinned-model resource (Ogre's Mesh+Skeleton resources):
// geometry with per-vertex bone ids/weights, the immutable Skeleton
// definition, and the animation clips. Load it once; every
// SkinnedMeshInstance points here — five characters with different
// animations cost one copy of all of this plus five small instances.
//
// Loads the user's own .mesh format (MESH/SKEL/SKIN chunks from the
// exporter) and .anim files for the clips.
class SkinnedMesh
{
public:
    SkinnedMesh() = default;
    ~SkinnedMesh();
    SkinnedMesh(const SkinnedMesh&) = delete;
    SkinnedMesh& operator=(const SkinnedMesh&) = delete;

    // .mesh with SKEL+SKIN chunks (resolved through the fs search folders)
    bool load(const char* meshPath);
    // appends the clips of one .anim file; call once per animation file
    bool load_animations(const char* animPath);

    bool is_loaded() const { return m_loaded; }
    Mesh& mesh() { return m_mesh; }
    const Skeleton& skeleton() const { return m_skeleton; }
    const std::vector<AnimationClip*>& clips() const { return m_clips; }
    const AnimationClip* find_clip(const std::string& name) const;

    // per-vertex skinning data (SKIN chunk), same order as the mesh verts;
    // uploaded as an extra vertex stream by ensure_gpu()
    struct VertexWeights
    {
        gl::u8 bone[4];
        float weight[4];
    };
    const std::vector<VertexWeights>& weights() const { return m_weights; }

    bool ensure_gpu();  // uploads geometry + the weights stream
    void release_gpu();

private:
    Mesh m_mesh;
    Skeleton m_skeleton;
    std::vector<AnimationClip*> m_clips; // owned
    std::vector<VertexWeights> m_weights;
    gl::Buffer m_weightsVbo; // extra stream: locations 4 (ids) + 5 (weights)
    bool m_loaded = false;
    bool m_gpuReady = false;
};
