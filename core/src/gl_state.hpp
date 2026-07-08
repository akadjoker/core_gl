#pragma once

// ---------------------------------------------------------------------------
// INTERNAL — central GL state cache, shared by all library modules.
// Every Bind* returns true when the GL call was actually made (cache miss).
// Stats are updated here so the numbers stay consistent everywhere.
// ---------------------------------------------------------------------------

#include "gl_platform.hpp"
#include "coregl/gl_types.hpp"

namespace gl
{
namespace state
{

RenderStats& Stats();

// true between Renderer::Init and Renderer::Shutdown. Destructors must check
// this before touching GL: after the context is gone (window closed), calling
// glDelete* is undefined behaviour — and the dead context frees everything
// anyway.
bool ContextAlive();

bool BindProgram(u32 id);
bool BindVAO(u32 id); // invalidates the cached element buffer (VAO-scoped state)
bool BindBuffer(BufferType type, u32 id);
bool BindTexture(u32 unit, GLenum target, u32 id);
bool BindFBO(u32 id);

// Index buffer type of the currently bound VAO (set by VertexArray)
void SetIndexType(GLenum glType, u32 byteSize);
GLenum IndexTypeGL();
u32 IndexTypeSize();

GLenum BufferTarget(BufferType type);

} // namespace state
} // namespace gl
