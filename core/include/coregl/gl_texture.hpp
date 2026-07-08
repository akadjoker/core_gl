#pragma once

#include "gl_types.hpp"

namespace gl
{

class Texture
{
    u32 id = 0;
    u32 target = 0; // GL_TEXTURE_2D / GL_TEXTURE_CUBE_MAP (internal GL value)
    int width = 0;
    int height = 0;
    TextureFormat format = TextureFormat::RGBA8;

public:
    Texture();
    ~Texture();
    Texture(Texture&& other) noexcept;
    Texture& operator=(Texture&& other) noexcept;
    Texture(const Texture&) = delete;
    Texture& operator=(const Texture&) = delete;
 
    void Release();

    // 2D texture from raw data; data may be nullptr (render target)
    void Load2D(const void* data, int w, int h, TextureFormat format);

    // Cubemap: 6 faces (+X, -X, +Y, -Y, +Z, -Z), each size x size
    void LoadCube(const void* data[6], int size, TextureFormat format);

    // Depth texture (shadow maps)
    void LoadDepth(int w, int h, TextureFormat format = TextureFormat::DEPTH32F);

    // Depth cubemap (point light shadows)
    void LoadDepthCube(int size, TextureFormat format = TextureFormat::DEPTH32F);

    void Bind(u32 unit);
    void Unbind(u32 unit);

    void SetWrap(TextureWrap s, TextureWrap t, TextureWrap r = TextureWrap::REPEAT);
    void SetFilter(TextureFilter minFilter, TextureFilter magFilter);
    void GenerateMipmaps();

    int GetWidth() const { return width; }
    int GetHeight() const { return height; }
    TextureFormat GetFormat() const { return format; }
    u32 GetHandle() const { return id; }
    bool IsValid() const { return id != 0; }
    bool IsCube() const;
};

} // namespace gl
