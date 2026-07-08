#include "coregl/gl_renderbuffer.hpp"
#include "gl_platform.hpp"
#include "gl_state.hpp"

namespace gl
{

// indexed by TextureFormat — same internal formats as gl_texture.cpp's table,
// duplicated here since renderbuffers don't need the format/type pair (only
// the internal format matters for glRenderbufferStorage)
static const GLenum kInternalFormat[] = {
    GL_R8,
    GL_R16F,
    GL_R32F,
    GL_RG8,
    GL_RG16F,
    GL_RG32F,
    GL_RGB8,
    GL_RGB16F,
    GL_RGB32F,
    GL_RGBA8,
    GL_RGBA16F,
    GL_RGBA32F,
    GL_SRGB8,
    GL_SRGB8_ALPHA8,
    GL_DEPTH_COMPONENT16,
    GL_DEPTH_COMPONENT24,
    GL_DEPTH_COMPONENT32F,
    GL_DEPTH24_STENCIL8,
};

RenderBuffer::RenderBuffer() = default;

void RenderBuffer::Release()
{
    if (id && state::ContextAlive()) glDeleteRenderbuffers(1, &id);
    id = 0;
    width = height = 0;
    samples = 1;
}

RenderBuffer::~RenderBuffer()
{
    Release();
}

RenderBuffer::RenderBuffer(RenderBuffer&& other) noexcept
    : id(other.id), width(other.width), height(other.height), samples(other.samples),
      format(other.format)
{
    other.id = 0;
}

RenderBuffer& RenderBuffer::operator=(RenderBuffer&& other) noexcept
{
    if (this != &other)
    {
        Release();
        id = other.id;
        width = other.width;
        height = other.height;
        samples = other.samples;
        format = other.format;
        other.id = 0;
    }
    return *this;
}

void RenderBuffer::Allocate(int w, int h, TextureFormat fmt, u32 sampleCount)
{
    if (!id) glGenRenderbuffers(1, &id);
    width = w;
    height = h;
    samples = sampleCount < 1 ? 1 : sampleCount;
    format = fmt;

    glBindRenderbuffer(GL_RENDERBUFFER, id);
    GLenum internalFormat = kInternalFormat[(u8)fmt];
    if (samples > 1)
        glRenderbufferStorageMultisample(GL_RENDERBUFFER, (GLsizei)samples, internalFormat, w, h);
    else
        glRenderbufferStorage(GL_RENDERBUFFER, internalFormat, w, h);
}

} // namespace gl
