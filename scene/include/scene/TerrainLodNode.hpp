#pragma once

#include "scene/MeshInstance.hpp"
#include "scene/Mesh.hpp"

// GeoMipMap terrain (inspired by Irrlicht's CTerrainSceneNode).
// A single heightmap is split into patches; each patch selects its LOD
// (geometric detail level) based on distance to the camera. At low LOD the
// patch renders fewer triangles (every 2^n-th vertex), at high LOD it
// renders the full grid. Neighbor patches at different LODs are stitched
// by snapping edge vertices to the coarser patch's step (T-junction fix).
//
// GPU strategy:
//   - ONE Mesh with DYNAMIC_DRAW VBO (vertices baked once in world space)
//   - DYNAMIC_DRAW IBO rebuilt every frame from the LOD selection
//   - ONE surface whose index_count is updated per frame
//
// The IBO is rebuilt only when the camera moves/rotates beyond thresholds,
// so a still camera costs zero CPU.
//
// Ported from the old engine's TerrainLod, adapted to the new scene's
// MeshInstance + Mesh dynamic-IBO API.
class TerrainLodNode : public MeshInstance
{
public:
    static constexpr NodeType ClassType = NT_TERRAINLOD;

    explicit TerrainLodNode(const std::string& name = "TerrainLod", int maxLOD = 4,
                            int patchSize = 17)
        : MeshInstance(name), m_maxLOD(maxLOD), m_patchSize(patchSize),
          m_calcPatchSize(patchSize - 1)
    {
        m_type = NT_TERRAINLOD;
    }
    ~TerrainLodNode() override;

    bool is_a(NodeType t) const override { return t == NT_TERRAINLOD || MeshInstance::is_a(t); }

    // Grayscale heightmap → terrain. heightScale maps [0,1]→[0,heightScale].
    // smoothFactor = box-blur passes on the height data.
    bool load_heightmap(const char* path, float heightScale = 1.f, int smoothFactor = 0);

    // Procedural path: height01 is size×size values in [0,1] (row-major, square).
    void build(const std::vector<float>& height01, int size, float heightScale);

    // Terrain scale (world size of the normalized 0..1 heightmap).
    void set_terrain_scale(const Vec3& s)
    {
        m_scale = s;
        apply_transformation();
    }
    void set_terrain_position(const Vec3& p)
    {
        m_pos = p;
        apply_transformation();
    }
    Vec3 get_terrain_scale() const { return m_scale; }
    Vec3 get_terrain_position() const { return m_pos; }

    // Texture tiling: texScale = how many times the diffuse repeats across
    // the terrain; detailScale = how many times the detail map repeats.
    void set_texture_scale(float s)
    {
        m_texScale = s;
        rebuild_vertices();
    }
    void set_detail_scale(float s)
    {
        m_detailScale = s;
        rebuild_vertices();
    }

    // The camera position must be provided each frame (before Scene::update)
    // to drive LOD selection and IBO rebuild.
    void set_camera_position(const Vec3& pos) { m_camPos = pos; }
    void set_camera_forward(const Vec3& fwd) { m_camFwd = fwd; }

    // CPU height queries (world space).
    float height_at(float wx, float wz) const;
    Vec3 normal_at(float wx, float wz) const;

    // Editing: raise/lower a circular area; smooth it. Triggers a rebake.
    void modify_height(float wx, float wz, float deltaHeight, float radius);
    void smooth_area(float wx, float wz, float radius, int iterations);

protected:
    void _update(float dt) override;
    void _release_gpu() override { m_terrainMesh.release_gpu(); }

private:
    struct Patch
    {
        BoundingBox aabb;
        Vec3 center{0, 0, 0};
        int currentLOD = 0;
        Patch* top = nullptr;
        Patch* bottom = nullptr;
        Patch* left = nullptr;
        Patch* right = nullptr;
    };

    float* m_heightData = nullptr;
    int m_size = 0;
    int m_patchSize = 17;
    int m_calcPatchSize = 16;
    int m_patchCount = 0;
    int m_maxLOD = 4;
    float m_heightScale = 1.f;
    float m_texScale = 1.f;
    float m_detailScale = 8.f;
    Vec3 m_pos{0, 0, 0};
    Vec3 m_scale{1, 1, 1};

    // baked vertices (world space) and source (normalized, for rebake)
    std::vector<MeshVertex> m_verts;
    std::vector<MeshVertex> m_sourceVerts;
    std::vector<u32> m_indices;
    u32 m_indicesToRender = 0;
    u32 m_maxIndices = 0;

    std::vector<Patch> m_patches;
    std::vector<float> m_lodDist;
    BoundingBox m_aabb;

    // camera tracking to avoid per-frame recalc when standing still
    Vec3 m_camPos{0, 0, 0};
    Vec3 m_camFwd{0, 0, -1};
    Vec3 m_oldCamPos{0, 0, 0};
    Vec3 m_oldCamFwd{0, 0, -1};
    float m_camMoveDelta = 10.f;
    float m_camRotDelta = 0.9998f;
    bool m_forceRecalc = true;

    Mesh m_terrainMesh; // owned; dynamic VBO + IBO, single surface

    float sample_height(int x, int z) const;
    void calculate_normals();
    void apply_transformation();
    void calculate_distance_thresholds();
    void create_patches();
    void calculate_patch_data();
    bool pre_render_lod();
    u32 get_index(int patchX, int patchZ, int patchIdx, u32 vX, u32 vZ) const;
    void bake_positions();   // source (normalized) -> baked world verts
    void rebuild_vertices(); // rebuilds source+baked verts from height data
    void rebake()
    {
        apply_transformation();
        m_forceRecalc = true;
    }
    static void bb_expand(BoundingBox& bb, const Vec3& p, bool& first);
};
