#pragma once

#include "gl_types.hpp"

namespace gl
{

class Texture
{
    u32 id = 0;
    u32 target = 0; // GL_TEXTURE_2D / _CUBE_MAP / _2D_ARRAY (internal GL value)
    int width = 0;
    int height = 0;
    int layers = 0; // > 0 only for array textures
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

    // 2D array: `layerCount` slices of w x h, one program-visible object
    // (sampler2DArray in GLSL). `data` is layerCount images back to back
    // (w*h*bytesPerPixel each); may be nullptr for a render target
    // (shadow cascades, splat maps you fill later via FrameBuffer layers).
    void LoadArray(const void* data, int w, int h, int layerCount, TextureFormat format);

    // Depth array (cascaded shadow maps: one layer per cascade)
    void LoadDepthArray(int w, int h, int layerCount,
                        TextureFormat format = TextureFormat::DEPTH32F);

    void Bind(u32 unit);
    void Unbind(u32 unit);

    void SetWrap(TextureWrap s, TextureWrap t, TextureWrap r = TextureWrap::REPEAT);
    void SetFilter(TextureFilter minFilter, TextureFilter magFilter);

    // Remaps which channel each of R/G/B/A reads from — e.g. a single-channel
    // height map (Load2D with TextureFormat::R8) sampled as .rrr1 by a shader
    // that expects RGBA: SetSwizzle(RED, RED, RED, ONE). Purely a sampling
    // remap; doesn't touch the stored data.
    void SetSwizzle(TextureSwizzle r, TextureSwizzle g, TextureSwizzle b, TextureSwizzle a);

    void GenerateMipmaps();

    // Uploads one mip level of a 2D texture directly (precomputed mip
    // chains — e.g. a prefiltered IBL map with roughness baked per level —
    // where GenerateMipmaps()'s box filter isn't what you want). `data`
    // covers w x h texels (the size of that level); must be called after
    // Load2D.
    void UploadMipLevel(u32 level, const void* data, int w, int h);

    int GetWidth() const { return width; }
    int GetHeight() const { return height; }
    int GetLayerCount() const { return layers; }
    TextureFormat GetFormat() const { return format; }
    u32 GetHandle() const { return id; }
    bool IsValid() const { return id != 0; }
    bool IsCube() const;
    bool IsArray() const;
};

} // namespace gl
