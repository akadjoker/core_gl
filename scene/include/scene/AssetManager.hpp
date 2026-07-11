#pragma once

#include <coregl/gl_config.hpp>
#include <coregl/gl_shader.hpp>
#include <coregl/gl_texture.hpp>

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

    void clear();

    gl::u32 textureCount() const;
    gl::u32 shaderCount() const;

private:
    AssetManager();
    ~AssetManager();
    AssetManager(const AssetManager&) = delete;
    AssetManager& operator=(const AssetManager&) = delete;

    struct Impl;
    Impl* m_impl;
};

} // namespace assets