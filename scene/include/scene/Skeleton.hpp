#pragma once

#include "scene/Math.hpp"
#include <coregl/gl_types.hpp>
#include <string>
#include <vector>

// One bone of the skeleton DEFINITION (shared between every instance of a
// SkinnedMesh — immutable after load). Flat array + parent indices: no node
// per bone (Ogre pays a Node per bone per entity; we evaluate linearly).
// Parents always precede children in the array (exporter guarantees it), so
// evaluation is a single forward pass.
struct Bone
{
    std::string name;
    gl::i32 parent = -1;   // index into the bone array, -1 = root
    Mat4 bindLocal;        // local transform at bind time (SKEL chunk)
    Mat4 inverseBind;      // world->bone at bind time (SKEL chunk)
};

// A bone's animated local transform. Clips sample into arrays of these;
// blending mixes them; Skeleton::evaluate turns them into skinning matrices.
struct LocalPose
{
    Vec3 position = Vec3(0.f, 0.f, 0.f);
    Quaternion rotation;
    Vec3 scale = Vec3(1.f, 1.f, 1.f);
};

// Skeleton definition: the bone hierarchy + bind data, loaded from the
// .mesh SKEL chunk. Shared, read-only at runtime. Per-instance state
// (poses, palettes) lives in SkinnedMeshInstance.
class Skeleton
{
public:
    bool empty() const { return m_bones.empty(); }
    int bone_count() const { return (int)m_bones.size(); }
    const Bone& bone(int i) const { return m_bones[(size_t)i]; }
    int find_bone(const char* name) const; // -1 if missing

    // fills `out` with each bone's bind-time local pose (the rest pose —
    // also the starting point for blending)
    void bind_pose(LocalPose* out) const;

    // locals -> global matrices -> skinning palette (global * inverseBind).
    // `globals` may be null if only the palette is wanted; both arrays are
    // bone_count() long. Single forward pass (parents precede children).
    void evaluate(const LocalPose* locals, Mat4* globals, Mat4* palette) const;

    // building (MeshLoader/SkinnedMesh use this while parsing SKEL)
    void add_bone(const char* name, gl::i32 parent, const Mat4& bindLocal,
                  const Mat4& inverseBind);

private:
    std::vector<Bone> m_bones;
};
