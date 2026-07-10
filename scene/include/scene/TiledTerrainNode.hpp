#pragma once

#include "scene/MeshInstance.hpp"
#include "scene/Mesh.hpp"

// Flat tile-based terrain with a tile atlas. A tilemap (grid of u8 tile IDs)
// is converted into patches of quads; each quad picks its UVs from a tile in
// a texture atlas (tilesInSide × tilesInSide tiles). Each patch becomes one
// SURFACE of a single Mesh, so the scene's per-surface frustum culling culls
// invisible patches automatically.
//
// Ported from the old engine's TiledTerrain. The vertex layout is the
// standard MeshVertex (pos/normal/tangent/uv). uv2 (detail) from the old
// engine is packed into a second surface concept — here we use the standard
// uv for atlas lookup, matching the forward shader's single-UV design.
class TiledTerrainNode : public MeshInstance
{
public:
    static constexpr NodeType ClassType = NT_TILEDTERRAIN;

    TiledTerrainNode(int tilesInSide = 8, float patchLen = 1.f, int tilesPerPatch = 1,
                     u8 defaultTile = 0, const std::string& name = "TiledTerrain")
        : MeshInstance(name), m_tilesInSide(tilesInSide), m_patchLen(patchLen),
          m_tilesPerPatch(tilesPerPatch), m_defaultTile(defaultTile)
    {
        m_type = NT_TILEDTERRAIN;
    }
    ~TiledTerrainNode() override;

    bool is_a(NodeType t) const override { return t == NT_TILEDTERRAIN || MeshInstance::is_a(t); }

    // Loads a tilemap: w×h grid of u8 tile IDs (0..tilesInSide²-1).
    // The terrain covers w*tileWorld × h*tileWorld world units.
    void load_tilemap(u32 w, u32 h, const u8* data);

    int get_tiles_in_side() const { return m_tilesInSide; }
    float get_patch_len() const { return m_patchLen; }

protected:
    void _release_gpu() override { m_terrainMesh.release_gpu(); }

private:
    void rebuild_patches();

    u32 m_mapW = 0, m_mapH = 0;
    u8* m_tileMap = nullptr;
    int m_tilesInSide = 8;
    int m_tilesPerPatch = 1;
    float m_patchLen = 1.f;
    u8 m_defaultTile = 0;
    Mesh m_terrainMesh; // owned; one surface per patch
};
