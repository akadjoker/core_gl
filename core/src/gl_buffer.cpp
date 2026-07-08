#include "coregl/gl_buffer.hpp"
#include "gl_platform.hpp"
#include "gl_state.hpp"
#include <cstring>

namespace gl
{

static const GLenum kUsage[] = {GL_STREAM_DRAW,  GL_STREAM_READ,  GL_STREAM_COPY,
                                GL_STATIC_DRAW,  GL_STATIC_READ,  GL_STATIC_COPY,
                                GL_DYNAMIC_DRAW, GL_DYNAMIC_READ, GL_DYNAMIC_COPY};

// Data operations (Allocate/Upload/Download) on an ELEMENT_ARRAY buffer must
// NOT go through the GL_ELEMENT_ARRAY_BUFFER binding point: that binding is
// part of the currently bound VAO's state, so e.g. allocating cube indices
// while some other mesh's VAO is still bound would silently overwrite that
// VAO's index buffer (observed as "reversed winding" — the mesh was drawing
// with another mesh's indices). GL buffers are untyped; the target only
// matters at VertexArray::SetIndexBuffer time, so plain data work uses the
// ARRAY target, which is global state and touches no VAO.
static BufferType dataBindType(BufferType t)
{
    return t == BufferType::ELEMENT_ARRAY ? BufferType::ARRAY : t;
}

Buffer::Buffer() = default;

void Buffer::Release()
{
    // after Renderer::Shutdown the dead context already freed the object;
    // only the id reset matters then
    if (id && state::ContextAlive()) glDeleteBuffers(1, &id);
    id = 0;
    type = BufferType::UNKNOWN;
    byteSize = 0;
}

Buffer::~Buffer()
{
    Release();
}

Buffer::Buffer(Buffer&& other) noexcept
    : id(other.id), type(other.type), usage(other.usage), byteSize(other.byteSize)
{
    other.id = 0;
    other.byteSize = 0;
}

Buffer& Buffer::operator=(Buffer&& other) noexcept
{
    if (this != &other)
    {
        Release();
        id = other.id;
        type = other.type;
        usage = other.usage;
        byteSize = other.byteSize;
        other.id = 0;
        other.byteSize = 0;
    }
    return *this;
}

void Buffer::Allocate(BufferType bufferType, const void* data, size_t size, UsageType usageType)
{
    if (!id) glGenBuffers(1, &id);
    type = bufferType;
    usage = usageType;
    byteSize = size;
    BufferType bt = dataBindType(type);
    state::BindBuffer(bt, id);
    glBufferData(state::BufferTarget(bt), (GLsizeiptr)size, data, kUsage[(u8)usageType]);
}

void Buffer::Upload(const void* data, size_t size, size_t offset)
{
    if (!id) return;
    BufferType bt = dataBindType(type);
    state::BindBuffer(bt, id);
    glBufferSubData(state::BufferTarget(bt), (GLintptr)offset, (GLsizeiptr)size, data);
}

void Buffer::Download(void* out, size_t size, size_t offset)
{
    if (!id) return;
    BufferType bt = dataBindType(type);
    state::BindBuffer(bt, id);
#if defined(CORE_GL_ES)
    // ES has no glGetBufferSubData: map instead
    const void* src = glMapBufferRange(state::BufferTarget(bt), (GLintptr)offset, (GLsizeiptr)size,
                                       GL_MAP_READ_BIT);
    if (src)
    {
        memcpy(out, src, size);
        glUnmapBuffer(state::BufferTarget(bt));
    }
#else
    glGetBufferSubData(state::BufferTarget(bt), (GLintptr)offset, (GLsizeiptr)size, out);
#endif
}

void Buffer::Bind()
{
    state::BindBuffer(type, id);
}

void Buffer::Unbind()
{
    state::BindBuffer(type, 0);
}

void Buffer::BindBase(u32 index)
{
    if (!id) return;
    // only valid for SHADER_STORAGE / UNIFORM targets
    glBindBufferBase(state::BufferTarget(type), index, id);
    ++state::Stats().bufferBinds;
}

} // namespace gl
