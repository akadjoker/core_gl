#pragma once

#include "scene/Math.hpp"
#include <coregl/gl_config.hpp>
#include <coregl/gl_shader.hpp>
#include <coregl/gl_texture.hpp>

#include <string>
#include <vector>

class Mesh;
class SkinnedMesh;
class Material;
struct MorphKeyframes;
struct MorphTags;
namespace volume
{
class Source;
}

namespace assets
{

class AssetManager
{
public:
    static AssetManager& instance();

    bool init();
    void release();


    void set_flip_on_lood(bool flip);

    // Never returns null: a missing/corrupt file logs a warning and yields
    // the shared checkerboard fallback — the scene keeps rendering.
    gl::Texture* loadTexture(const char* name, const char* path, bool sRGB = false);
    // skybox cubemap from 6 image files, order: +X -X +Y -Y +Z -Z
    // (right, left, up, down, front, back). Faces must be square, same
    // size; failure falls back to the checkerboard like loadTexture.
    gl::Texture* loadCubemap(const char* name, const char* const paths[6]);
    // shared magenta/black checker (lazily built)
    gl::Texture* defaultTexture();
    // registers an empty texture owned by the manager — for procedurally
    // generated content (the caller fills it with Load2D/LoadArray/...)
    gl::Texture* createTexture(const char* name);
    gl::Texture* getTexture(const char* name);
    void unloadTexture(const char* name);

    gl::Shader* loadShader(const char* name, const char* vertPath, const char* fragPath);
    gl::Shader* loadShaderFromString(const char* name, const char* vertSource,
                                     const char* fragSource);
    gl::Shader* getShader(const char* name);
    void unloadShader(const char* name);

    // ── meshes (the manager is the single owner of geometry memory) ──
    // create empty (fill + upload yourself), load .h3d, or make primitives;
    // all are uploaded on creation except createMesh. Name collisions
    // return the existing mesh, like textures.
    Mesh* createMesh(const char* name);
    Mesh* loadMesh(const char* name, const char* path);
    Mesh* getMesh(const char* name);
    Mesh* createCube(const char* name, float sx, float sy, float sz);
    Mesh* createPlane(const char* name, float width, float depth, float uvTiles = 1.f,
                     int segX = 1, int segZ = 1);
    Mesh* createSphere(const char* name, float radius, int rings = 16, int slices = 24);
    Mesh* createCylinder(const char* name, float radius, float height, int slices = 24);
    Mesh* createCone(const char* name, float radius, float height, int slices = 24);
    Mesh* createCapsule(const char* name, float radius, float height, int rings = 8,
                        int slices = 24);
    Mesh* createHillsPlane(const char* name, float width, float depth, int segX, int segZ,
                           float (*heightFn)(float x, float z), float uvTiles = 1.f);
    // brute-force single-mesh terrain from a heightmap array (see
    // primitives::heightfield — small patches/previews )
    Mesh* createHeightfield(const char* name, const float* heights, int w, int h,
                           float cellSize, float uvTiles = 1.f);

    // ── skinned meshes + their animation clips ──
    // loadSkinnedMesh loads the .h3d (SKEL/SKIN); loadAnimation appends one
    // .anim clip to a skinned mesh previously loaded under `name`.
    SkinnedMesh* loadSkinnedMesh(const char* name, const char* meshPath);
    SkinnedMesh* getSkinnedMesh(const char* name);
    bool loadAnimation(const char* name, const char* animPath);


    // Loads a Quake 3 BSP map (IBSP v46).  Polygon and mesh faces are
    // imported directly; Bezier patches are tessellated (level 5).  Vertices
    // are converted from Z-up to Y-up.  One Material is created per texture
    // group and pushed into `out_mats`; the Mesh takes ownership of them
    // (Mesh::set_owned_materials — freed with the mesh in AssetManager::
    // clear()/dtor). `out_mats` stays a valid view for the caller to tweak
    // (e.g. assign a texture) but must not be deleted by it.
    // `textureDir` is prepended to texture names when loading image files;
    // .tga/.jpg/.png/.bmp are tried in that order.
    // Returns nullptr on I/O or parse failure (error is logged).
    Mesh* load_bsp_mesh(const char* name, const char* path,
                     std::vector<Material*>& out_mats,
                     const char* textureDir = "");

    // Loads a Quake 2 MD2 (.md2) — vertex/morph animation, single surface,
    // no tags. Builds the mesh with upload_dynamic(); the caller drives it
    // afterward via a MorphAnimator + Mesh::update_vertices() (see
    // MorphMeshInstance). One Material (the skin), owned by the Mesh (see
    // load_bsp_mesh's comment above) — `out_mats` is a view, not a caller
    // owner. Returns the existing mesh if `name` was already loaded.
    Mesh* load_md2_mesh(const char* name, const char* path, MorphKeyframes& outAnim,
                        std::vector<Material*>& out_mats, const char* textureDir = "");

    // Loads a Quake 3 MD3 (.md3) — multi-surface vertex/morph animation
    // with per-frame tags (named attachment points used to glue separate
    // model parts together, e.g. lower->tag_torso->upper). `outTags` may
    // be null if the caller doesn't need attachment points. One Material
    // per surface, owned by the Mesh (see load_bsp_mesh's comment above) —
    // `out_mats` is a view, not a caller owner.
    Mesh* load_md3_mesh(const char* name, const char* path, MorphKeyframes& outAnim,
                        std::vector<Material*>& out_mats, MorphTags* outTags = nullptr,
                        const char* textureDir = "");

    // Loads a static Blitz3D (.b3d) mesh — geometry only, no skeleton or
    // animation: each NODE's own transform is baked directly into its
    // vertices (recursively through parent pivots), so nested static parts
    // come out correctly positioned without any skinning. One Material per
    // brush (BRUS/TEXS chunks), owned by the Mesh (see load_bsp_mesh's
    // comment above) — `out_mats` is a view, not a caller owner. For
    // animated .b3d (bone hierarchy + ANIM/KEYS chunks) use
    // SkinnedMesh::load()/load_animations() instead, which recognizes the
    // same "BB3D" magic automatically (see AssetManager::loadSkinnedMesh).
    Mesh* load_b3d_mesh(const char* name, const char* path,
                       std::vector<Material*>& out_mats, const char* textureDir = "");

    // Extracts the isosurface (density == 0) of a volume::Source via
    // marching cubes over a regular grid spanning [from, to], with
    // voxelSize-sized cubes — see scene/VolumeSource.hpp/VolumeCSGSource.hpp
    // for building density fields (caves, tunnels, smooth CSG blobs) and
    // scene/VolumeMesher.cpp for the extraction itself. Vertex normals come
    // straight from the source's analytic density gradient. Like every
    // other loader, the Mesh is owned by the manager (freed in clear()/
    // dtor); returns the existing mesh if `name` was already built.
    Mesh* build_volume_mesh(const char* name, const volume::Source& src, const Vec3& from, const Vec3& to,
                            float voxelSize);

    // Same extraction as build_volume_mesh, but uploads with spare GPU
    // capacity (upload_dynamic()) so later edits of the density field can
    // be pushed with update_volume_mesh() instead of building a whole new
    // Mesh — for live digging/sculpting. `capacityFactor` (>= 1) reserves
    // that many times the initial vert/index count; a later
    // update_volume_mesh() call whose new geometry doesn't fit that
    // capacity fails (logs and returns false) rather than growing it, so
    // pick a factor generous enough for the edits you expect. Surface
    // bounds cover the whole [from, to] query region (not just the current
    // geometry), since that stays a valid, if loose, box across edits that
    // stay within the same region.
    Mesh* build_volume_mesh_dynamic(const char* name, const volume::Source& src, const Vec3& from,
                                    const Vec3& to, float voxelSize, float capacityFactor = 2.0f);

    // Re-extracts `src` over [from, to] and pushes the new geometry into
    // `mesh` in place (Mesh::update_vertices/update_indices +
    // set_dynamic_index_count) instead of allocating a new Mesh. `mesh`
    // must have come from build_volume_mesh_dynamic(). Returns false
    // (no changes made) if the new geometry exceeds the reserved capacity.
    bool update_volume_mesh(Mesh* mesh, const volume::Source& src, const Vec3& from, const Vec3& to,
                            float voxelSize);

    void clear();

    gl::u32 textureCount() const;
    gl::u32 shaderCount() const;
    gl::u32 meshCount() const;

private:
    AssetManager();
    ~AssetManager();
    AssetManager(const AssetManager&) = delete;
    AssetManager& operator=(const AssetManager&) = delete;

    struct Impl;
    Impl* m_impl;
};

} // namespace assets