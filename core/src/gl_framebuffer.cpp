#include "coregl/gl_framebuffer.hpp"
#include "coregl/gl_log.hpp"
#include "coregl/gl_texture.hpp"
#include "coregl/gl_renderbuffer.hpp"
#include "gl_platform.hpp"
#include "gl_state.hpp"

namespace gl
{

static GLenum attachmentPoint(Attachment a)
{
    if ((u8)a <= (u8)Attachment::COLOR15) return GL_COLOR_ATTACHMENT0 + (u8)a;
    if (a == Attachment::DEPTH) return GL_DEPTH_ATTACHMENT;
    if (a == Attachment::STENCIL) return GL_STENCIL_ATTACHMENT;
    return GL_DEPTH_STENCIL_ATTACHMENT;
}

FrameBuffer::FrameBuffer() = default;

// created lazily so the object survives Release() + reuse, and can be
// declared before the GL context exists
static void ensureFBO(u32& id)
{
    if (!id) glGenFramebuffers(1, &id);
}

void FrameBuffer::Release()
{
    if (id && state::ContextAlive())
    {
        glDeleteFramebuffers(1, &id);
        state::OnFBODeleted(id);
    }
    id = 0;
    for (int i = 0; i < 19; ++i)
        attachments[i] = AttachmentInfo();
}

FrameBuffer::~FrameBuffer()
{
    Release();
}

FrameBuffer::FrameBuffer(FrameBuffer&& other) noexcept : id(other.id)
{
    for (int i = 0; i < 19; ++i)
        attachments[i] = other.attachments[i];
    other.id = 0;
}

FrameBuffer& FrameBuffer::operator=(FrameBuffer&& other) noexcept
{
    if (this != &other)
    {
        Release();
        id = other.id;
        for (int i = 0; i < 19; ++i)
            attachments[i] = other.attachments[i];
        other.id = 0;
    }
    return *this;
}

void FrameBuffer::Bind()
{
    ensureFBO(id);
    state::BindFBO(id);
}

void FrameBuffer::Unbind()
{
    state::BindFBO(0);
}

void FrameBuffer::AttachTexture(const Texture& tex, Attachment attachment, u32 mipLevel)
{
    ensureFBO(id);
    state::BindFBO(id);
    glFramebufferTexture2D(GL_FRAMEBUFFER, attachmentPoint(attachment), GL_TEXTURE_2D,
                           tex.GetHandle(), (GLint)mipLevel);
    attachments[(u8)attachment].texId = tex.GetHandle();
    attachments[(u8)attachment].used = 1;
}

void FrameBuffer::AttachCubeFace(const Texture& tex, Attachment attachment, u32 face, u32 mipLevel)
{
    ensureFBO(id);
    state::BindFBO(id);
    glFramebufferTexture2D(GL_FRAMEBUFFER, attachmentPoint(attachment),
                           GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, tex.GetHandle(), (GLint)mipLevel);
    attachments[(u8)attachment].texId = tex.GetHandle();
    attachments[(u8)attachment].used = 1;
}

void FrameBuffer::AttachTextureLayer(const Texture& tex, Attachment attachment, u32 layer,
                                     u32 mipLevel)
{
    ensureFBO(id);
    state::BindFBO(id);
    glFramebufferTextureLayer(GL_FRAMEBUFFER, attachmentPoint(attachment), tex.GetHandle(),
                              (GLint)mipLevel, (GLint)layer);
    attachments[(u8)attachment].texId = tex.GetHandle();
    attachments[(u8)attachment].used = 1;
}

void FrameBuffer::AttachRenderBuffer(const RenderBuffer& rb, Attachment attachment)
{
    ensureFBO(id);
    state::BindFBO(id);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, attachmentPoint(attachment), GL_RENDERBUFFER,
                              rb.GetHandle());
    attachments[(u8)attachment].texId = rb.GetHandle();
    attachments[(u8)attachment].used = 1;
}

void FrameBuffer::Detach(Attachment attachment)
{
    state::BindFBO(id);
    glFramebufferTexture2D(GL_FRAMEBUFFER, attachmentPoint(attachment), GL_TEXTURE_2D, 0, 0);
    attachments[(u8)attachment].texId = 0;
    attachments[(u8)attachment].used = 0;
}

void FrameBuffer::SetDrawBuffers()
{
    state::BindFBO(id);
    GLenum buffers[16];
    GLsizei count = 0;
    for (u8 i = 0; i <= (u8)Attachment::COLOR15; ++i)
        if (attachments[i].used) buffers[count++] = GL_COLOR_ATTACHMENT0 + i;

    if (count == 0)
    {
        GLenum none = GL_NONE; // depth-only pass (shadow maps)
        glDrawBuffers(1, &none);
    }
    else
    {
        glDrawBuffers(count, buffers);
    }
}

bool FrameBuffer::IsComplete()
{
    state::BindFBO(id);
    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE)
        Log::Error("framebuffer %u incomplete: 0x%04x", id, status);
    return status == GL_FRAMEBUFFER_COMPLETE;
}

} // namespace gl
