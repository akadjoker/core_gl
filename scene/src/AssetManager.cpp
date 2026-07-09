#include "scene/AssetManager.hpp"
#include "scene/Filesystem.hpp"

#include <cstring>

// stb_image implementation lives in stb_impl.cpp
#include "stb_image.h"

#include <unordered_map>
#include <string>

namespace assets
{

struct AssetManager::Impl
{
    std::unordered_map<std::string, gl::Texture*> textures;
    std::unordered_map<std::string, gl::Shader*> shaders;
};

AssetManager::AssetManager()
    : m_impl(new Impl)
{
}

AssetManager::~AssetManager()
{
    release();
    delete m_impl;
}

AssetManager& AssetManager::instance()
{
    static AssetManager inst;
    return inst;
}

bool AssetManager::init()
{
    return true;
}

void AssetManager::release()
{
    clear();
}

void AssetManager::clear()
{
    for (auto& pair : m_impl->textures)
    {
        if (pair.second)
        {
            pair.second->Release();
            delete pair.second;
        }
    }
    m_impl->textures.clear();

    for (auto& pair : m_impl->shaders)
    {
        if (pair.second)
        {
            pair.second->Release();
            delete pair.second;
        }
    }
    m_impl->shaders.clear();
}

gl::Texture* AssetManager::loadTexture(const char* name, const char* path, bool sRGB)
{
    if (!name || !path) return nullptr;

    auto it = m_impl->textures.find(name);
    if (it != m_impl->textures.end())
        return it->second;

    scene::ByteArray data;
    if (!fs::getFilesystem().readFile(path, data))
        return nullptr;

    int w = 0, h = 0, channels = 0;
    stbi_uc* pixels = stbi_load_from_memory(
        data.data(), (int)data.size(), &w, &h, &channels, 4);

    data.destroy();

    if (!pixels)
        return nullptr;

    gl::Texture* tex = new gl::Texture();
    tex->Load2D(pixels, w, h,
                sRGB ? gl::TextureFormat::SRGB8_ALPHA8 : gl::TextureFormat::RGBA8);
    stbi_image_free(pixels);

    m_impl->textures[name] = tex;
    return tex;
}

gl::Texture* AssetManager::getTexture(const char* name)
{
    if (!name) return nullptr;
    auto it = m_impl->textures.find(name);
    return (it != m_impl->textures.end()) ? it->second : nullptr;
}

void AssetManager::unloadTexture(const char* name)
{
    if (!name) return;
    auto it = m_impl->textures.find(name);
    if (it != m_impl->textures.end())
    {
        if (it->second)
        {
            it->second->Release();
            delete it->second;
        }
        m_impl->textures.erase(it);
    }
}

gl::Shader* AssetManager::loadShader(const char* name, const char* vertPath, const char* fragPath)
{
    if (!name || !vertPath || !fragPath) return nullptr;

    auto it = m_impl->shaders.find(name);
    if (it != m_impl->shaders.end())
        return it->second;

    scene::ByteArray vs, fs;
    if (!fs::getFilesystem().readText(vertPath, vs)) return nullptr;
    if (!fs::getFilesystem().readText(fragPath, fs))
    {
        vs.destroy();
        return nullptr;
    }

    gl::Shader* shader = new gl::Shader();
    bool ok = shader->LoadFromString(gl::PipelineStage::VERTEX, (const char*)vs.data()) &&
              shader->LoadFromString(gl::PipelineStage::FRAGMENT, (const char*)fs.data()) &&
              shader->Link();

    vs.destroy();
    fs.destroy();

    if (!ok)
    {
        delete shader;
        return nullptr;
    }

    m_impl->shaders[name] = shader;
    return shader;
}

gl::Shader* AssetManager::loadShaderFromString(const char* name, const char* vertSource, const char* fragSource)
{
    if (!name || !vertSource || !fragSource) return nullptr;

    auto it = m_impl->shaders.find(name);
    if (it != m_impl->shaders.end())
        return it->second;

    gl::Shader* shader = new gl::Shader();
    bool ok = shader->LoadFromString(gl::PipelineStage::VERTEX, vertSource) &&
              shader->LoadFromString(gl::PipelineStage::FRAGMENT, fragSource) &&
              shader->Link();

    if (!ok)
    {
        delete shader;
        return nullptr;
    }

    m_impl->shaders[name] = shader;
    return shader;
}

gl::Shader* AssetManager::getShader(const char* name)
{
    if (!name) return nullptr;
    auto it = m_impl->shaders.find(name);
    return (it != m_impl->shaders.end()) ? it->second : nullptr;
}

void AssetManager::unloadShader(const char* name)
{
    if (!name) return;
    auto it = m_impl->shaders.find(name);
    if (it != m_impl->shaders.end())
    {
        if (it->second)
        {
            it->second->Release();
            delete it->second;
        }
        m_impl->shaders.erase(it);
    }
}

gl::u32 AssetManager::textureCount() const
{
    return (gl::u32)m_impl->textures.size();
}

gl::u32 AssetManager::shaderCount() const
{
    return (gl::u32)m_impl->shaders.size();
}

} // namespace assets