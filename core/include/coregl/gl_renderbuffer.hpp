#pragma once

#include "gl_types.hpp"

namespace gl
{

// A GL renderbuffer: storage for a FrameBuffer attachment that is written
// to but never sampled in a shader (unlike Texture). Cheaper than a texture
// for that case, and the only way to get a multisampled attachment — MSAA
// works by rendering into renderbuffers with samples > 1, then resolving
// with Renderer::BlitFramebuffer into a regular (single-sample) Texture
// before the result is used anywhere (post-processing, display, sampling).
class RenderBuffer
{
    u32 id = 0;
    int width = 0;
    int height = 0;
    u32 samples = 1;
    TextureFormat format = TextureFormat::RGBA8;

public:
    RenderBuffer();
    ~RenderBuffer();
    RenderBuffer(RenderBuffer&& other) noexcept;
    RenderBuffer& operator=(RenderBuffer&& other) noexcept;
    RenderBuffer(const RenderBuffer&) = delete;
    RenderBuffer& operator=(const RenderBuffer&) = delete;

    // Frees the GL object now; the destructor is only a safety net
    void Release();

    // samples = 1 for a regular renderbuffer; > 1 for MSAA (clamped to the
    // driver's GL_MAX_SAMPLES if it's asked for more than that)
    void Allocate(int w, int h, TextureFormat format, u32 samples = 1);

    int GetWidth() const { return width; }
    int GetHeight() const { return height; }
    u32 GetSamples() const { return samples; }
    TextureFormat GetFormat() const { return format; }
    u32 GetHandle() const { return id; }
    bool IsValid() const { return id != 0; }
};

} // namespace gl
