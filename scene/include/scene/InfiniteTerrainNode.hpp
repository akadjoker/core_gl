#pragma once

#include "scene/MeshInstance.hpp"
#include "scene/Mesh.hpp"
#include "scene/Pixmap.hpp"

class Material;
#include <unordered_map>

// Infinite terrain that tiles a base heightmap endlessly around the camera.
// Patches are created on-demand in a ring around the camera, each with LOD
// based on distance. A cache (LRU eviction) holds patch meshes so moving
// around doesn't rebuild visible patches every frame.
//
// Each cached patch has up to MAX_LOD pre-built Mesh variants (different
// vertex densities). The _update() method determines which patches are
// visible, builds missing ones, creates child MeshInstance nodes to hold
// them, and evicts old patches beyond MAX_CACHED.
//
// Skirts (vertical walls around each patch) hide T-junctions between
// patches of different LOD.
class InfiniteTerrainNode : public Node3D
{
public:
    static constexpr NodeType ClassType = NT_INFINITETERRAIN;
    static constexpr int MAX_CACHED = 512;
    static constexpr int MAX_LOD = 4;

    explicit InfiniteTerrainNode(const std::string& name = "InfiniteTerrain") : Node3D(name)
    {
        m_type = NT_INFINITETERRAIN;
    }
    ~InfiniteTerrainNode() override;

    bool is_a(NodeType t) const override { return t == NT_INFINITETERRAIN || Node3D::is_a(t); }

    // Loads a tileable grayscale heightmap. heightScale maps [0,1]→[0,heightScale].
    bool load_base_heightmap(const char* path, float heightScale = 100.f);

    // Configuration: visibleHalf = half-extent of the patch ring around the
    // camera (so (2*visibleHalf+1)² patches are visible).
    // vertsPerPatch = grid resolution per patch (e.g. 33).
    // patchWorld = world-space size of each patch.
    // heightmapWorld = world units one repetition of the base heightmap
    // covers (the terrain SHAPE must not depend on view distance settings).
    void configure(int visibleHalf, int vertsPerPatch, float patchWorld,
                   float heightmapWorld = 1024.f);

    // material applied to every terrain patch (non-owning)
    void set_material(Material* m);

    // The scene/game must tell the terrain where the camera is each frame
    // (before Scene::update). This drives which patches are visible.
    void set_camera_position(const Vec3& pos) { m_camPosition = pos; }

    // CPU height query at any world position (wraps the base heightmap).
    float height_at(float wx, float wz) const;

protected:
    void _update(float dt) override;
    void _release_gpu() override;

private:
    struct PatchEntry
    {
        Mesh meshes[MAX_LOD] = {}; // up to MAX_LOD mesh variants
        bool built[MAX_LOD] = {};
        BoundingBox aabb;
        u32 lastFrame = 0;
        int activeLod = -1;
        // child node holding the currently-visible mesh. The TREE owns it
        // (create_child): eviction detaches it first, the destructor never
        // deletes it — deleting here would double-free through ~Node.
        MeshInstance* node = nullptr;
    };

    // base heightmap data
    float* m_heights = nullptr;
    u32 m_hmW = 0, m_hmH = 0;
    float m_hmScale = 100.f;

    int m_visibleHalf = 4;
    int m_vertsPerPatch = 33;
    float m_patchWorld = 64.f;
    float m_hmWorldSize = 1024.f; // world span of one heightmap repetition
    Material* m_material = nullptr;

    Vec3 m_camPosition;

    u32 m_frame = 0;
    std::unordered_map<long long, PatchEntry*> m_cache;

    float sample_base(float u, float v) const;
    Vec3 calc_normal_uv(float u, float v) const;
    int calc_lod(float distSq) const;
    PatchEntry* get_or_create(int px, int pz);
    void build_mesh(PatchEntry* patch, int px, int pz, int lod);
    void evict_old();
    long long patch_key(int px, int pz) const { return ((long long)px << 32) | (u32)(u32)pz; }
};
