#pragma once

#include "scene/MeshInstance.hpp"
#include "scene/Mesh.hpp"

// Block-based heightmap terrain, ported from the old engine's TerrainNode.
// The heightmap becomes blocks of 33x33 vertices, each with its own AABB —
// here every block is one SURFACE of a single Mesh, so the scene's
// per-surface frustum culling does exactly what the old per-block cull did,
// and the terrain renders through the normal collect()/forward path
// (shadows, sky, water reflections all just work).
//
// Queries stay on the CPU heightfield: getHeightAt/getNormalAt for placing
// things on the ground, raycast for picking.
struct TerrainRaycastResult
{
    bool hit = false;
    Vec3 position = Vec3(0.f, 0.f, 0.f);
    Vec3 normal = Vec3(0.f, 1.f, 0.f);
    float distance = 0.f;
};

class TerrainNode : public MeshInstance
{
public:
    static constexpr NodeType ClassType = NT_TERRAIN;
    static constexpr int BLOCK_VERTS = 33;

    explicit TerrainNode(const std::string& name = "Terrain") : MeshInstance(name)
    {
        m_type = NT_TERRAIN;
    }
    ~TerrainNode() override;

    bool is_a(NodeType t) const override { return t == NT_TERRAIN || MeshInstance::is_a(t); }

    // Grayscale heightmap image -> terrain of sx x sz world units, sy tall.
    // texU/texV tile the diffuse texture across the whole terrain.
    bool load_heightmap(const char* path, float sx, float sy, float sz, float texU = 1.f,
                        float texV = 1.f);

    // Procedural path: height01 is size*size values in [0,1] (row-major).
    bool build(const float* height01, int size, float sx, float sy, float sz, float texU = 1.f,
               float texV = 1.f);

    // ── CPU heightfield queries (world space, terrain at the origin) ──
    float height_at(float wx, float wz) const; // bilinear
    Vec3 normal_at(float wx, float wz) const;  // nearest sample
    TerrainRaycastResult raycast(const Ray& ray, float maxDist) const;

protected:
    void _release_gpu() override { m_terrainMesh.release_gpu(); }

private:
    bool build_blocks(float texU, float texV);
    float sample_height(int x, int z) const; // clamped, already scaled by sy
    Vec3 calc_normal(int x, int z) const;

    float* m_heightData = nullptr; // normalized [0,1]
    int m_mapW = 0, m_mapH = 0;
    Vec3 m_terrainScale = Vec3(1.f, 1.f, 1.f);
    Mesh m_terrainMesh; // owned; one surface per block
};
