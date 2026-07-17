#pragma once

#include "scene/Mesh.hpp"
#include "scene/Skeleton.hpp"
#include "scene/AnimationClip.hpp"
#include <memory>
#include <string>
#include <vector>

class Material;


// The SHARED skinned-model resource (Ogre's Mesh+Skeleton resources):
// geometry with per-vertex bone ids/weights, the immutable Skeleton
// definition, and the animation clips. Load it once; every
// SkinnedMeshInstance points here — five characters with different
// animations cost one copy of all of this plus five small instances.
//
// Loads .mesh format (MESH/SKEL/SKIN chunks from the
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

    // per-material metadata from the MATS chunk (name/colors/texture
    // filenames as recorded at export time) — index i corresponds exactly
    // to Surface::material_slot == i on mesh().surfaces(), same convention
    // as the exporter's own material list.
    struct MaterialInfo
    {
        std::string name;
        Vec3 diffuse{1.f, 1.f, 1.f};
        Vec3 specular{0.f, 0.f, 0.f};
        float shininess = 32.f;
        std::vector<std::string> textures; // usually just the diffuse map, [0]
    };
    const std::vector<MaterialInfo>& material_infos() const { return m_materialInfos; }

    // real, ready-to-use Material* built automatically at load() time from
    // material_infos() — textures loaded via AssetManager, resolved
    // relative to the mesh file's own directory. Owned by SkinnedMesh
    // (freed in the destructor); index i matches Surface::material_slot
    // exactly. SkinnedMeshInstance::set_mesh() copies this in as the
    // default, so `instance->set_mesh(res)` alone is already correctly
    // textured — no per-demo material-building loop needed.
    const std::vector<Material*>& materials() const { return m_materials; }

    gl::u32 material_count() const { return (gl::u32)m_materials.size(); }

    Material* get_material(int slot) const
    {
        return (slot >= 0 && slot < (int)m_materials.size()) ? m_materials[(size_t)slot] : nullptr;
    }

    bool set_material_texture(int slot, gl::Texture* tex);
    

    bool ensure_gpu();  // uploads geometry + the weights stream
    void release_gpu();

private:
    // glTF dispatch targets — load()/load_animations() sniff the file's
    // first bytes and call these instead when it's glTF, not the native
    // chunk format (see GLTFLoader.cpp). Private: callers never need to
    // know which format they got.
    bool load_gltf(const char* path);
    bool load_animations_gltf(const char* path);

    // Blitz3D (.b3d, "BB3D" magic) dispatch targets — animated/skeletal
    // path (see B3DLoader.cpp). For a static .b3d with no bones, use
    // AssetManager::load_b3d_mesh() instead.
    bool load_b3d(const char* path);
    bool load_animations_b3d(const char* path);

    // Inter-Quake Model (.iqm v2, "INTERQUAKEMODEL" magic) dispatch
    // targets (see IQMLoader.cpp).
    bool load_iqm(const char* path);
    bool load_animations_iqm(const char* path);

    Mesh m_mesh;
    Skeleton m_skeleton;
    std::vector<AnimationClip*> m_clips; // owned
    std::vector<VertexWeights> m_weights;
    std::vector<MaterialInfo> m_materialInfos;
    std::vector<Material*> m_materials; // owned; built from m_materialInfos at load() time
    gl::Buffer m_weightsVbo; // extra stream: locations 4 (ids) + 5 (weights)
    bool m_loaded = false;
    bool m_gpuReady = false;
};
