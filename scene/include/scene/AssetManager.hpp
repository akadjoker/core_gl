#pragma once

#include <coregl/gl_config.hpp>
#include <coregl/gl_shader.hpp>
#include <coregl/gl_texture.hpp>

class Mesh;
class SkinnedMesh;

namespace assets
{

class AssetManager
{
public:
    static AssetManager& instance();

    bool init();
    void release();

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