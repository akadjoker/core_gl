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

    gl::Texture* loadTexture(const char* name, const char* path, bool sRGB = false);
    gl::Texture* getTexture(const char* name);
    void unloadTexture(const char* name);

    gl::Shader* loadShader(const char* name, const char* vertPath, const char* fragPath);
    gl::Shader* loadShaderFromString(const char* name, const char* vertSource, const char* fragSource);
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