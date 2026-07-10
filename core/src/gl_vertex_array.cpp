#include "coregl/gl_vertex_array.hpp"
#include "coregl/gl_buffer.hpp"
#include "gl_platform.hpp"
#include "gl_state.hpp"

namespace gl
{

static const GLenum kAttribType[] = {GL_BYTE, GL_UNSIGNED_BYTE, GL_SHORT, GL_UNSIGNED_SHORT,
                                     GL_INT,  GL_UNSIGNED_INT,  GL_FLOAT, GL_HALF_FLOAT};

static const u32 kAttribSize[] = {1, 1, 2, 2, 4, 4, 4, 2};

static bool isIntegerType(VertexAttribType t)
{
    return t != VertexAttribType::FLOAT && t != VertexAttribType::HALF_FLOAT;
}

VertexArray::VertexArray() = default;

// created lazily so the object survives Release() + reuse, and can be
// declared before the GL context exists
static void ensureVAO(u32& id)
{
    if (!id) glGenVertexArrays(1, &id);
}

void VertexArray::Release()
{
    if (id && state::ContextAlive())
    {
        glDeleteVertexArrays(1, &id);
        state::OnVAODeleted(id);
    }
    id = 0;
    attribIndex = 0;
}

VertexArray::~VertexArray()
{
    Release();
}

VertexArray::VertexArray(VertexArray&& other) noexcept
    : id(other.id), attribIndex(other.attribIndex), indexType(other.indexType)
{
    other.id = 0;
}

VertexArray& VertexArray::operator=(VertexArray&& other) noexcept
{
    if (this != &other)
    {
        Release();
        id = other.id;
        attribIndex = other.attribIndex;
        indexType = other.indexType;
        other.id = 0;
    }
    return *this;
}

void VertexArray::Bind()
{
    ensureVAO(id);
    if (state::BindVAO(id))
    {
        // let DrawIndexed know which index type this VAO uses
        state::SetIndexType(kAttribType[(u8)indexType], kAttribSize[(u8)indexType]);
    }
}

void VertexArray::Unbind()
{
    state::BindVAO(0);
}

static void addBuffer(u32& vao, u32& attribIndex, const Buffer& vbo, const VertexAttrib* attribs,
                      u32 attribCount, u32 stride, bool perInstance)
{
    ensureVAO(vao);
    state::BindVAO(vao);
    state::BindBuffer(BufferType::ARRAY, vbo.GetHandle());

    size_t offset = 0;
    for (u32 i = 0; i < attribCount; ++i)
    {
        const VertexAttrib& a = attribs[i];
        glEnableVertexAttribArray(attribIndex);

        if (isIntegerType(a.type) && !a.normalized)
        {
            glVertexAttribIPointer(attribIndex, a.components, kAttribType[(u8)a.type],
                                   (GLsizei)stride, (const void*)offset);
        }
        else
        {
            glVertexAttribPointer(attribIndex, a.components, kAttribType[(u8)a.type],
                                  a.normalized ? GL_TRUE : GL_FALSE, (GLsizei)stride,
                                  (const void*)offset);
        }

        u32 divisor = perInstance ? 1 : a.divisor;
        if (divisor) glVertexAttribDivisor(attribIndex, divisor);

        offset += (size_t)kAttribSize[(u8)a.type] * a.components;
        ++attribIndex;
    }
}

void VertexArray::AddVertexBuffer(const Buffer& vbo, const VertexAttrib* attribs, u32 attribCount,
                                  u32 stride)
{
    addBuffer(id, attribIndex, vbo, attribs, attribCount, stride, false);
}

void VertexArray::AddInstanceBuffer(const Buffer& vbo, const VertexAttrib* attribs, u32 attribCount,
                                    u32 stride)
{
    addBuffer(id, attribIndex, vbo, attribs, attribCount, stride, true);
}

void VertexArray::SetIndexBuffer(const Buffer& ibo, VertexAttribType type)
{
    indexType = type;
    ensureVAO(id);
    state::BindVAO(id);
    state::BindBuffer(BufferType::ELEMENT_ARRAY, ibo.GetHandle());
    state::SetIndexType(kAttribType[(u8)type], kAttribSize[(u8)type]);
}

} // namespace gl
