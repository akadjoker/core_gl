#include "scene/AssetManager.hpp"
#include "scene/Filesystem.hpp"
#include <coregl/gl_log.hpp>

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
    gl::Texture* fallback = nullptr; // checkerboard, owned via `textures`
};

AssetManager::AssetManager() : m_impl(new Impl) {}

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
    m_impl->fallback = nullptr;

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
    if (it != m_impl->textures.end()) return it->second;

    scene::ByteArray data;
    if (!fs::getFilesystem().readFile(path, data))
    {
        gl::Log::Warn("AssetManager: texture '%s' not found ('%s') — using checkerboard", name,
                      path);
        return defaultTexture();
    }

    int w = 0, h = 0, channels = 0;
    stbi_uc* pixels = stbi_load_from_memory(data.data(), (int)data.size(), &w, &h, &channels, 4);

    data.destroy();

    if (!pixels)
    {
        gl::Log::Warn("AssetManager: texture '%s' failed to decode ('%s') — using checkerboard",
                      name, path);
        return defaultTexture();
    }

    gl::Texture* tex = new gl::Texture();
    tex->Load2D(pixels, w, h, sRGB ? gl::TextureFormat::SRGB8_ALPHA8 : gl::TextureFormat::RGBA8);
    // sane defaults for a diffuse map: tile and mip (terrain UVs go way
    // past 1; CLAMP would smear the last texel row across the ground)
    tex->SetWrap(gl::TextureWrap::REPEAT, gl::TextureWrap::REPEAT);
    tex->GenerateMipmaps();
    tex->SetFilter(gl::TextureFilter::LINEAR_MIPMAP_LINEAR, gl::TextureFilter::LINEAR);
    stbi_image_free(pixels);

    m_impl->textures[name] = tex;
    return tex;
}

gl::Texture* AssetManager::loadCubemap(const char* name, const char* const paths[6])
{
    if (!name || !paths) return nullptr;

    auto it = m_impl->textures.find(name);
    if (it != m_impl->textures.end()) return it->second;

    stbi_uc* faces[6] = {};
    int size = 0;
    bool ok = true;
    for (int i = 0; i < 6 && ok; ++i)
    {
        scene::ByteArray data;
        if (!fs::getFilesystem().readFile(paths[i], data))
        {
            gl::Log::Warn("AssetManager: cubemap '%s' face '%s' not found", name, paths[i]);
            ok = false;
            break;
        }
        int w = 0, h = 0, channels = 0;
        faces[i] = stbi_load_from_memory(data.data(), (int)data.size(), &w, &h, &channels, 4);
        data.destroy();
        if (!faces[i] || w != h || (size && w != size))
        {
            gl::Log::Warn("AssetManager: cubemap '%s' face '%s' bad (%dx%d, need square, equal)",
                          name, paths[i], w, h);
            ok = false;
            break;
        }
        size = w;
    }

    if (!ok)
    {
        for (int i = 0; i < 6; ++i)
            if (faces[i]) stbi_image_free(faces[i]);
        return defaultTexture();
    }

    const void* data6[6];
    for (int i = 0; i < 6; ++i)
        data6[i] = faces[i];
    gl::Texture* tex = new gl::Texture();
    tex->LoadCube(data6, size, gl::TextureFormat::RGBA8);
    tex->SetFilter(gl::TextureFilter::LINEAR, gl::TextureFilter::LINEAR);
    tex->SetWrap(gl::TextureWrap::CLAMP_TO_EDGE, gl::TextureWrap::CLAMP_TO_EDGE,
                 gl::TextureWrap::CLAMP_TO_EDGE);
    for (int i = 0; i < 6; ++i)
        stbi_image_free(faces[i]);

    m_impl->textures[name] = tex;
    return tex;
}

gl::Texture* AssetManager::defaultTexture()
{
    if (m_impl->fallback) return m_impl->fallback;
    // 64x64 magenta/black checker: unmistakably "asset missing"
    const int N = 64;
    static gl::u8 px[N * N * 4];
    for (int y = 0; y < N; ++y)
        for (int x = 0; x < N; ++x)
        {
            bool on = ((x / 8) + (y / 8)) & 1;
            gl::u8* p = &px[(y * N + x) * 4];
            p[0] = on ? 255 : 0;
            p[1] = 0;
            p[2] = on ? 255 : 0;
            p[3] = 255;
        }
    gl::Texture* tex = new gl::Texture();
    tex->Load2D(px, N, N, gl::TextureFormat::RGBA8);
    tex->SetWrap(gl::TextureWrap::REPEAT, gl::TextureWrap::REPEAT);
    m_impl->textures["__checker"] = tex;
    m_impl->fallback = tex;
    return tex;
}

gl::Texture* AssetManager::createTexture(const char* name)
{
    if (!name) return nullptr;
    auto it = m_impl->textures.find(name);
    if (it != m_impl->textures.end()) return it->second;
    gl::Texture* tex = new gl::Texture();
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
    if (it != m_impl->shaders.end()) return it->second;

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

gl::Shader* AssetManager::loadShaderFromString(const char* name, const char* vertSource,
                                               const char* fragSource)
{
    if (!name || !vertSource || !fragSource) return nullptr;

    auto it = m_impl->shaders.find(name);
    if (it != m_impl->shaders.end()) return it->second;

    gl::Shader* shader = new gl::Shader();
    bool ok = shader->LoadFromString(gl::PipelineStage::VERTEX, vertSource) &&
              shader->LoadFromString(gl::PipelineStage::FRAGMENT, fragSource) && shader->Link();

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