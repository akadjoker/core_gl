#include "coregl/gl_texture.hpp"
#include "gl_platform.hpp"
#include "gl_state.hpp"

namespace gl
{

// indexed by TextureFormat
struct FormatInfo
{
    GLenum internal;
    GLenum format;
    GLenum type;
};

static const FormatInfo kFormat[] = {
    {GL_R8, GL_RED, GL_UNSIGNED_BYTE},
    {GL_R16F, GL_RED, GL_HALF_FLOAT},
    {GL_R32F, GL_RED, GL_FLOAT},
    {GL_RG8, GL_RG, GL_UNSIGNED_BYTE},
    {GL_RG16F, GL_RG, GL_HALF_FLOAT},
    {GL_RG32F, GL_RG, GL_FLOAT},
    {GL_RGB8, GL_RGB, GL_UNSIGNED_BYTE},
    {GL_RGB16F, GL_RGB, GL_HALF_FLOAT},
    {GL_RGB32F, GL_RGB, GL_FLOAT},
    {GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE},
    {GL_RGBA16F, GL_RGBA, GL_HALF_FLOAT},
    {GL_RGBA32F, GL_RGBA, GL_FLOAT},
    {GL_SRGB8, GL_RGB, GL_UNSIGNED_BYTE},
    {GL_SRGB8_ALPHA8, GL_RGBA, GL_UNSIGNED_BYTE},
    {GL_DEPTH_COMPONENT16, GL_DEPTH_COMPONENT, GL_UNSIGNED_SHORT},
    {GL_DEPTH_COMPONENT24, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT},
    {GL_DEPTH_COMPONENT32F, GL_DEPTH_COMPONENT, GL_FLOAT},
    {GL_DEPTH24_STENCIL8, GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8},
};

static const GLenum kWrap[] = {
    GL_REPEAT, GL_MIRRORED_REPEAT, GL_CLAMP_TO_EDGE,
#if defined(CORE_GL_ES)
    GL_CLAMP_TO_EDGE, // CLAMP_TO_BORDER: ES 3.2+ only, fall back
#else
    GL_CLAMP_TO_BORDER,
#endif
};

static const GLenum kFilter[] = {GL_NEAREST,
                                 GL_LINEAR,
                                 GL_NEAREST_MIPMAP_NEAREST,
                                 GL_LINEAR_MIPMAP_NEAREST,
                                 GL_NEAREST_MIPMAP_LINEAR,
                                 GL_LINEAR_MIPMAP_LINEAR};

Texture::Texture() = default;

void Texture::Release()
{
    if (id && state::ContextAlive()) glDeleteTextures(1, &id);
    id = 0;
    target = 0;
    width = height = layers = 0;
}

Texture::~Texture()
{
    Release();
}

Texture::Texture(Texture&& other) noexcept
    : id(other.id), target(other.target), width(other.width), height(other.height),
      layers(other.layers), format(other.format)
{
    other.id = 0;
}

Texture& Texture::operator=(Texture&& other) noexcept
{
    if (this != &other)
    {
        Release();
        id = other.id;
        target = other.target;
        width = other.width;
        height = other.height;
        layers = other.layers;
        format = other.format;
        other.id = 0;
    }
    return *this;
}

bool Texture::IsCube() const
{
    return target == GL_TEXTURE_CUBE_MAP;
}

bool Texture::IsArray() const
{
    return target == GL_TEXTURE_2D_ARRAY;
}

// creates (or recreates) the GL object and binds it to unit 0 for setup
static u32 createAndBind(u32& id, u32& target, GLenum newTarget)
{
    if (id && target != newTarget)
    {
        glDeleteTextures(1, &id);
        id = 0;
    }
    if (!id) glGenTextures(1, &id);
    target = newTarget;
    state::BindTexture(0, newTarget, id);
    return id;
}

void Texture::Load2D(const void* data, int w, int h, TextureFormat fmt)
{
    createAndBind(id, target, GL_TEXTURE_2D);
    width = w;
    height = h;
    format = fmt;

    const FormatInfo& f = kFormat[(u8)fmt];
    glTexImage2D(GL_TEXTURE_2D, 0, f.internal, w, h, 0, f.format, f.type, data);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

void Texture::LoadCube(const void* data[6], int size, TextureFormat fmt)
{
    createAndBind(id, target, GL_TEXTURE_CUBE_MAP);
    width = size;
    height = size;
    format = fmt;

    const FormatInfo& f = kFormat[(u8)fmt];
    for (int face = 0; face < 6; ++face)
    {
        glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, 0, f.internal, size, size, 0, f.format,
                     f.type, data ? data[face] : nullptr);
    }
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
}

void Texture::LoadDepth(int w, int h, TextureFormat fmt)
{
    createAndBind(id, target, GL_TEXTURE_2D);
    width = w;
    height = h;
    format = fmt;

    const FormatInfo& f = kFormat[(u8)fmt];
    glTexImage2D(GL_TEXTURE_2D, 0, f.internal, w, h, 0, f.format, f.type, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

void Texture::LoadDepthCube(int size, TextureFormat fmt)
{
    createAndBind(id, target, GL_TEXTURE_CUBE_MAP);
    width = size;
    height = size;
    format = fmt;

    const FormatInfo& f = kFormat[(u8)fmt];
    for (int face = 0; face < 6; ++face)
    {
        glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, 0, f.internal, size, size, 0, f.format,
                     f.type, nullptr);
    }
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
}

void Texture::LoadArray(const void* data, int w, int h, int layerCount, TextureFormat fmt)
{
    createAndBind(id, target, GL_TEXTURE_2D_ARRAY);
    width = w;
    height = h;
    layers = layerCount;
    format = fmt;

    const FormatInfo& f = kFormat[(u8)fmt];
    glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, f.internal, w, h, layerCount, 0, f.format, f.type, data);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

void Texture::LoadDepthArray(int w, int h, int layerCount, TextureFormat fmt)
{
    createAndBind(id, target, GL_TEXTURE_2D_ARRAY);
    width = w;
    height = h;
    layers = layerCount;
    format = fmt;

    const FormatInfo& f = kFormat[(u8)fmt];
    glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, f.internal, w, h, layerCount, 0, f.format, f.type,
                 nullptr);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

void Texture::Bind(u32 unit)
{
    if (id) state::BindTexture(unit, target, id);
}

void Texture::Unbind(u32 unit)
{
    if (id) state::BindTexture(unit, target, 0);
}

void Texture::SetWrap(TextureWrap sWrap, TextureWrap tWrap, TextureWrap rWrap)
{
    if (!id) return;
    state::BindTexture(0, target, id);
    glTexParameteri(target, GL_TEXTURE_WRAP_S, kWrap[(u8)sWrap]);
    glTexParameteri(target, GL_TEXTURE_WRAP_T, kWrap[(u8)tWrap]);
    if (target == GL_TEXTURE_CUBE_MAP) glTexParameteri(target, GL_TEXTURE_WRAP_R, kWrap[(u8)rWrap]);
}

void Texture::SetFilter(TextureFilter minFilter, TextureFilter magFilter)
{
    if (!id) return;
    state::BindTexture(0, target, id);
    glTexParameteri(target, GL_TEXTURE_MIN_FILTER, kFilter[(u8)minFilter]);
    glTexParameteri(target, GL_TEXTURE_MAG_FILTER, kFilter[(u8)magFilter]);
}

void Texture::SetSwizzle(TextureSwizzle r, TextureSwizzle g, TextureSwizzle b, TextureSwizzle a)
{
    if (!id) return;
    static const GLenum kSwizzle[] = {GL_RED, GL_GREEN, GL_BLUE, GL_ALPHA, GL_ZERO, GL_ONE};
    state::BindTexture(0, target, id);
    GLint mask[4] = {(GLint)kSwizzle[(u8)r], (GLint)kSwizzle[(u8)g], (GLint)kSwizzle[(u8)b],
                     (GLint)kSwizzle[(u8)a]};
    glTexParameteriv(target, GL_TEXTURE_SWIZZLE_RGBA, mask);
}

void Texture::GenerateMipmaps()
{
    if (!id) return;
    state::BindTexture(0, target, id);
    glGenerateMipmap(target);
}

void Texture::UploadMipLevel(u32 level, const void* data, int w, int h)
{
    if (!id) return;
    state::BindTexture(0, target, id);
    const FormatInfo& f = kFormat[(u8)format];
    glTexImage2D(target, (GLint)level, f.internal, w, h, 0, f.format, f.type, data);
}

} // namespace gl
