#pragma once

#include "gl_types.hpp"

namespace gl
{

class Texture;
class RenderBuffer;

class FrameBuffer
{
    u32 id = 0;

    struct AttachmentInfo
    {
        u32 texId = 0;
        u8 used = 0;
    };
    AttachmentInfo attachments[19]; // 16 color + depth + stencil + depth_stencil

public:
    FrameBuffer();
    ~FrameBuffer();
    FrameBuffer(FrameBuffer&& other) noexcept;
    FrameBuffer& operator=(FrameBuffer&& other) noexcept;
    FrameBuffer(const FrameBuffer&) = delete;
    FrameBuffer& operator=(const FrameBuffer&) = delete;

    // Frees the GL object now; the destructor is only a safety net
    void Release();

    void Bind();
    void Unbind(); // back to default framebuffer (screen)

    void AttachTexture(const Texture& tex, Attachment attachment, u32 mipLevel = 0);

    // face: 0..5 (+X, -X, +Y, -Y, +Z, -Z)
    void AttachCubeFace(const Texture& tex, Attachment attachment, u32 face, u32 mipLevel = 0);

    // one layer of a 2D array texture (Texture::LoadArray/LoadDepthArray) —
    // e.g. render cascade N of a shadow map array by attaching layer N
    void AttachTextureLayer(const Texture& tex, Attachment attachment, u32 layer, u32 mipLevel = 0);

    // MSAA and other write-only attachments — see RenderBuffer. Renderbuffer
    // attachments can never be sampled in a shader; resolve with
    // Renderer::BlitFramebuffer into a Texture-backed FrameBuffer first.
    void AttachRenderBuffer(const RenderBuffer& rb, Attachment attachment);

    void Detach(Attachment attachment);

    // Sets glDrawBuffers to the attached color attachments (call after attaching)
    void SetDrawBuffers();

    bool IsComplete();

    u32 GetHandle() const { return id; }
    bool IsValid() const { return id != 0; }
};

} // namespace gl
