#include "pch.h"
#include "Vertex.hpp"
#include "Utils.hpp"


u32 CalculatePrimitiveCount(GLenum primitiveType, u32 vertexCount);

namespace
{
    VertexDrawStats g_vertexDrawStats;

    void AccumulateDrawStats(GLenum primitiveType, u32 count, u32 instanceCount, bool indexed)
    {
        g_vertexDrawStats.drawCalls++;
        g_vertexDrawStats.instances += instanceCount;
        if (indexed)
            g_vertexDrawStats.indexedDrawCalls++;
        if (instanceCount > 1)
            g_vertexDrawStats.instancedCalls++;

        const u32 totalCount = count * instanceCount;
        if (indexed)
            g_vertexDrawStats.submittedIndices += totalCount;
        else
            g_vertexDrawStats.submittedVertices += totalCount;

        switch (primitiveType)
        {
        case GL_POINTS:
            g_vertexDrawStats.points += totalCount;
            break;
        case GL_LINES:
            g_vertexDrawStats.lines += (count / 2) * instanceCount;
            break;
        case GL_LINE_STRIP:
            g_vertexDrawStats.lines += (count > 1 ? count - 1 : 0) * instanceCount;
            break;
        case GL_LINE_LOOP:
            g_vertexDrawStats.lines += count * instanceCount;
            break;
        case GL_TRIANGLES:
        case GL_TRIANGLE_STRIP:
        case GL_TRIANGLE_FAN:
            g_vertexDrawStats.triangles += CalculatePrimitiveCount(primitiveType, count) * instanceCount;
            break;
        default:
            break;
        }
    }

    void TrackedDrawElements(GLenum primitiveType, u32 count, GLenum idxType, const void *offset, u32 instanceCount = 1)
    {
        AccumulateDrawStats(primitiveType, count, instanceCount, true);
        if (instanceCount > 1)
            glDrawElementsInstanced(primitiveType, count, idxType, offset, instanceCount);
        else
            glDrawElements(primitiveType, count, idxType, offset);
    }

    void TrackedDrawArrays(GLenum primitiveType, u32 first, u32 count, u32 instanceCount = 1)
    {
        (void)first;
        AccumulateDrawStats(primitiveType, count, instanceCount, false);
        if (instanceCount > 1)
            glDrawArraysInstanced(primitiveType, first, count, instanceCount);
        else
            glDrawArrays(primitiveType, first, count);
    }
}

u32 CalculatePrimitiveCount(GLenum primitiveType, u32 vertexCount)
{
    switch (primitiveType)
    {
    case GL_POINTS:
        return vertexCount;
    case GL_LINES:
        return vertexCount / 2;
    case GL_LINE_STRIP:
        return vertexCount > 1 ? vertexCount - 1 : 0;
    case GL_LINE_LOOP:
        return vertexCount;
    case GL_TRIANGLES:
        return vertexCount / 3;
    case GL_TRIANGLE_STRIP:
    case GL_TRIANGLE_FAN:
        return vertexCount > 2 ? vertexCount - 2 : 0;
    default:
        return 0;
    }
}

bool ValidateVertexCount(GLenum primitiveType, u32 vertexCount)
{
    switch (primitiveType)
    {
    case GL_POINTS:
        return vertexCount >= 1;
    case GL_LINES:
        return vertexCount >= 2 && (vertexCount % 2 == 0);
    case GL_LINE_STRIP:
    case GL_LINE_LOOP:
        return vertexCount >= 2;
    case GL_TRIANGLES:
        return vertexCount >= 3 && (vertexCount % 3 == 0);
    case GL_TRIANGLE_STRIP:
    case GL_TRIANGLE_FAN:
        return vertexCount >= 3;
    default:
        return false;
    }
}

// ============================================================================
// VERTEX ELEMENT
// ============================================================================

VertexElement::VertexElement()
    : stream(0), offset(0), type(VET_FLOAT1), semantic(VES_POSITION), index(0), instanceDivisor(0)
{
}

u32 VertexElement::GetSize() const
{
    static const u32 sizes[] = {
        sizeof(float),     // VET_FLOAT1
        sizeof(float) * 2, // VET_FLOAT2
        sizeof(float) * 3, // VET_FLOAT3
        sizeof(float) * 4, // VET_FLOAT4
        4,                 // VET_COLOR
        sizeof(short) * 2, // VET_SHORT2
        sizeof(short) * 4, // VET_SHORT4
        4                  // VET_UBYTE4
    };
    return sizes[type];
}

u32 VertexElement::GetComponentCount() const
{
    static const u32 counts[] = {1, 2, 3, 4, 4, 2, 4, 4};
    return counts[type];
}

u32 VertexElement::GetType() const
{
    switch (type)
    {
    case VET_FLOAT1:
    case VET_FLOAT2:
    case VET_FLOAT3:
    case VET_FLOAT4:
        return GL_FLOAT;
    case VET_COLOR:
    case VET_UBYTE4:
        return GL_UNSIGNED_BYTE;
    case VET_SHORT2:
    case VET_SHORT4:
        return GL_SHORT;
    default:
        return GL_FLOAT;
    }
}

bool VertexElement::ShouldNormalize() const
{
    return type == VET_COLOR || type == VET_UBYTE4;
}

// ============================================================================
// VERTEX DECLARATION
// ============================================================================

VertexDeclaration::VertexDeclaration()
    : m_isDirty(true)
{
}

void VertexDeclaration::AddElement(u32 stream, u32 offset,
                                   VertexElementType type, VertexElementSemantic semantic,
                                   u32 index, u32 divisor)
{
    VertexElement elem;
    elem.stream = stream;
    elem.offset = offset;
    elem.type = type;
    elem.semantic = semantic;
    elem.index = index;
    elem.instanceDivisor = divisor;

    m_elements.push_back(elem);

    u32 endOffset = offset + elem.GetSize();
    auto &streamSize = m_streamSizes[stream];
    if (endOffset > streamSize)
    {
        streamSize = endOffset;
    }

    m_isDirty = true;
}

void VertexDeclaration::Clear()
{
    m_elements.clear();
    m_streamSizes.clear();
    m_isDirty = true;
}

u32 VertexDeclaration::GetVertexSize(u32 stream) const
{
    auto it = m_streamSizes.find(stream);
    return (it != m_streamSizes.end()) ? it->second : 0;
}

const std::vector<VertexElement> &VertexDeclaration::GetElements() const
{
    return m_elements;
}

const VertexElement *VertexDeclaration::FindElement(VertexElementSemantic semantic, u32 index) const
{
    for (const auto &elem : m_elements)
    {
        if (elem.semantic == semantic && elem.index == index)
        {
            return &elem;
        }
    }
    return nullptr;
}

// ============================================================================
// VERTEX BUFFER
// ============================================================================

VertexBuffer::VertexBuffer(u32 vSize, u32 vCount, bool dynamic)
    : m_vbo(0), m_vertexSize(vSize), m_vertexCount(vCount), m_isDynamic(dynamic)
{
    if (vSize == 0 || vCount == 0)
    {
        LogWarning("Creating VertexBuffer with zero size or count\n");
        return;
    }

    glGenBuffers(1, &m_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);

    GLenum usage = dynamic ? GL_DYNAMIC_DRAW : GL_STATIC_DRAW;
    glBufferData(GL_ARRAY_BUFFER, m_vertexSize * m_vertexCount, nullptr, usage);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

VertexBuffer::~VertexBuffer()
{
    if (m_vbo)
    {
        glDeleteBuffers(1, &m_vbo);
        m_vbo = 0;
    }
}

void VertexBuffer::SetData(const void *data)
{
    if (!IsValid() || !data)
    {
        LogWarning("Invalid VertexBuffer or null data in setData\n");
        return;
    }

    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);

    if (m_isDynamic)
    {
        glBufferSubData(GL_ARRAY_BUFFER, 0, m_vertexSize * m_vertexCount, data);
    }
    else
    {
        glBufferData(GL_ARRAY_BUFFER, m_vertexSize * m_vertexCount, data, GL_STATIC_DRAW);
    }

    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void VertexBuffer::SetSubData(u32 firstVertex, u32 vertexCount, const void *data)
{
    if (!IsValid() || !data)
    {
        LogWarning("Invalid VertexBuffer or null data in setSubData\n");
        return;
    }

    if (firstVertex + vertexCount > m_vertexCount)
    {
        LogError("VertexBuffer::setSubData - firstVertex(%u) + vertexCount(%u) exceeds buffer vertexCount(%u)\n",
                 firstVertex, vertexCount, m_vertexCount);
        return;
    }

    u32 byteOffset = firstVertex  * m_vertexSize;
    u32 byteSize   = vertexCount * m_vertexSize;

    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferSubData(GL_ARRAY_BUFFER, byteOffset, byteSize, data);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}


// void VertexBuffer::SetSubData(u32 offset, u32 size, const void *data)
// {
//     if (!IsValid() || !data)
//     {
//         LogWarning("Invalid VertexBuffer or null data in setSubData\n");
//         return;
//     }

//     if (offset + size > m_vertexSize * m_vertexCount)
//     {
//         LogError("VertexBuffer::setSubData - offset + size exceeds buffer size\n");
//         return;
//     }
//     u32 byteOffset = offset * m_vertexSize;
//     u32 byteSize = size * m_vertexSize;
//     CHECK_GL_ERROR(glBindBuffer(GL_ARRAY_BUFFER, m_vbo));
//     //CHECK_GL_ERROR(glBufferSubData(GL_ARRAY_BUFFER, offset, size, data));
//     CHECK_GL_ERROR(glBufferSubData(GL_ARRAY_BUFFER, byteOffset, byteSize, data));
//     CHECK_GL_ERROR(glBindBuffer(GL_ARRAY_BUFFER, 0));
// }

void VertexBuffer::Bind() const
{
    if (IsValid())
    {
        glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    }
}

// ============================================================================
// INDEX BUFFER
// ============================================================================

IndexBuffer::IndexBuffer(u32 iCount, bool dynamic, bool use16Bit)
    : m_ibo(0), m_indexCount(iCount), m_is16Bit(use16Bit), m_isDynamic(dynamic)
{
    if (iCount == 0)
    {
        LogWarning("Creating IndexBuffer with zero count\n");
        return;
    }

    glGenBuffers(1, &m_ibo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_ibo);

    size_t size = m_indexCount * (m_is16Bit ? sizeof(s16) : sizeof(u32));
    GLenum usage = dynamic ? GL_DYNAMIC_DRAW : GL_STATIC_DRAW;

    glBufferData(GL_ELEMENT_ARRAY_BUFFER, size, nullptr, usage);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
}

IndexBuffer::~IndexBuffer()
{
    if (m_ibo)
    {
        glDeleteBuffers(1, &m_ibo);
        m_ibo = 0;
    }
}

void IndexBuffer::SetData(const void *data)
{
    if (!IsValid() || !data)
    {
        LogWarning("Invalid IndexBuffer or null data in setData\n");
        return;
    }

    size_t size = m_indexCount * (m_is16Bit ? sizeof(uint16_t) : sizeof(u32));

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_ibo);

    if (m_isDynamic)
    {
        glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, 0, size, data);
    }
    else
    {
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, size, data, GL_STATIC_DRAW);
    }

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
}

void IndexBuffer::SetSubData(u32 offset, u32 count, const void *data)
{
    if (!IsValid() || !data)
    {
        LogWarning("Invalid IndexBuffer or null data in setSubData\n");
        return;
    }

    if (offset + count > m_indexCount)
    {
        LogError("IndexBuffer::setSubData -  count %d  exceeds buffer size %d", offset + count, m_indexCount);
        return;
    }

    size_t indexSize = m_is16Bit ? sizeof(uint16_t) : sizeof(u32);
    size_t byteOffset = offset * indexSize;
    size_t byteSize = count * indexSize;

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_ibo);
    glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, byteOffset, byteSize, data);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
}

void IndexBuffer::Bind() const
{
    if (IsValid())
    {
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_ibo);
    }
}

// void IndexBuffer::unbind()
// {
//     CHECK_GL_ERROR(glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0));
// }

u32 IndexBuffer::GetIndexType() const
{
    return m_is16Bit ? GL_UNSIGNED_SHORT : GL_UNSIGNED_INT;
}

// ============================================================================
// VERTEX ARRAY
// ============================================================================

VertexArray::VertexArray()
    : m_vao(0), m_vertexDeclaration(nullptr), m_indexBuffer(nullptr), m_isBuilt(false), m_needsRebuild(false)
{
    glGenVertexArrays(1, &m_vao);
    m_vertexDeclaration = new VertexDeclaration();
}

VertexArray::~VertexArray()
{
    Release();
}

VertexBuffer *VertexArray::AddVertexBuffer(u32 vSize, u32 vCount, bool dynamic)
{
    if (m_isBuilt)
    {
        LogWarning("Adding vertex buffer after VAO is built - marking for rebuild\n");
        m_needsRebuild = true;
    }

    VertexBuffer *vb = new VertexBuffer(vSize, vCount, dynamic);
    m_vertexBuffers.push_back(vb);
    return vb;
}

IndexBuffer *VertexArray::CreateIndexBuffer(u32 iCount, bool dynamic, bool use16Bit)
{
    if (m_indexBuffer)
    {
        delete m_indexBuffer;
        m_needsRebuild = true;
    }

    m_indexBuffer = new IndexBuffer(iCount, dynamic, use16Bit);
    return m_indexBuffer;
}

void VertexArray::Release()
{
    if (!IsValid())
    {
        return;
    }
    for (auto vb : m_vertexBuffers)
    {
        delete vb;
    }
    m_vertexBuffers.clear();

    if (m_vao)
    {
        glDeleteVertexArrays(1, &m_vao);
        m_vao = 0;
    }

    delete m_vertexDeclaration;
    delete m_indexBuffer;
}

void VertexArray::Build()
{
    if (!IsValid())
    {
        LogError("Cannot build invalid VertexArray\n");
        return;
    }

    glBindVertexArray(m_vao);

    // Get max vertex attributes supported
    GLint maxAttr = 0;
    glGetIntegerv(GL_MAX_VERTEX_ATTRIBS, &maxAttr);

    // Reset all attributes
    for (GLint i = 0; i < maxAttr; ++i)
    {
        glDisableVertexAttribArray(i);
        glVertexAttribDivisor(i, 0);
    }

    // Setup vertex attributes from declaration
    const auto &elements = m_vertexDeclaration->GetElements();
    GLint location = 0;
    for (const auto &elem : elements)
    {
        // Validate stream index
        if (elem.stream >= m_vertexBuffers.size())
        {
            LogWarning("Invalid stream %u in vertex element (only %zu streams available)\n",
                       elem.stream, m_vertexBuffers.size());
            continue;
        }

        // Check if we've exceeded max attributes
        if (location >= maxAttr)
        {
            LogError("Too many vertex attributes! Maximum is %d\n", maxAttr);
            break;
        }

        const VertexBuffer *vb = m_vertexBuffers[elem.stream];
        if (!vb || !vb->IsValid())
        {
            LogWarning("Invalid vertex buffer at stream %u\n", elem.stream);
            continue;
        }

        const GLsizei stride = static_cast<GLsizei>(m_vertexDeclaration->GetVertexSize(elem.stream));
        const void *offset = reinterpret_cast<const void *>(static_cast<uintptr_t>(elem.offset));

        vb->Bind();
        glEnableVertexAttribArray(location);

        // Use integer attribute pointer for blend indices
        const bool forceIntegerAttrib = (elem.semantic == VES_BLEND_INDICES);

        if (forceIntegerAttrib)
        {
            glVertexAttribIPointer(
                location,
                elem.GetComponentCount(),
                elem.GetType(),
                stride,
                offset);
        }
        else
        {
            glVertexAttribPointer(
                location,
                elem.GetComponentCount(),
                elem.GetType(),
                elem.ShouldNormalize() ? GL_TRUE : GL_FALSE,
                stride,
                offset);
        }

        glVertexAttribDivisor(location, elem.instanceDivisor);

        ++location;
    }

    // Bind index buffer (binding is part of VAO state)
    if (m_indexBuffer && m_indexBuffer->IsValid())
    {
        m_indexBuffer->Bind();
    }

    // Cleanup
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);

    m_vertexDeclaration->ClearDirty();
    m_isBuilt = true;
    m_needsRebuild = false;
}

void VertexArray::ensureBuilt() const
{
    if (!m_isBuilt || m_needsRebuild || m_vertexDeclaration->IsDirty())
    {
        const_cast<VertexArray *>(this)->Build();
    }
}

void VertexArray::Render(GLenum primitiveType, u32 count) const
{
    if (!IsValid() || m_vertexBuffers.empty() || count == 0)
    {
        return;
    }

    ensureBuilt();

    glBindVertexArray(m_vao);

    if (!ValidateVertexCount(primitiveType, count))
    {
        LogWarning("Invalid vertex count %u for primitive type 0x%x\n", count, primitiveType);
    }

    if (m_indexBuffer && m_indexBuffer->IsValid())
    {
        const GLenum idxType = m_indexBuffer->GetIndexType();
        TrackedDrawElements(primitiveType, count, idxType, nullptr);
    }
    else
    {
        TrackedDrawArrays(primitiveType, 0, count);
    }

    glBindVertexArray(0);
}


void VertexArray::Render(GLenum primitiveType, u32 count, u32 startIndex) const
{
    if (!IsValid() || m_vertexBuffers.empty() || count == 0)
    {
        return;
    }

    ensureBuilt();
    glBindVertexArray(m_vao);
 

    if (!ValidateVertexCount(primitiveType, count))
    {
        LogWarning("Invalid vertex count %u for primitive type 0x%x\n", count, primitiveType);
    }

    if (m_indexBuffer && m_indexBuffer->IsValid())
    {
        const GLenum idxType = m_indexBuffer->GetIndexType();

        size_t indexSize = (idxType == GL_UNSIGNED_SHORT) ? sizeof(u16) : sizeof(u32);
        const void* offset = (const void*)(startIndex * indexSize);

        TrackedDrawElements(primitiveType, count, idxType, offset);
    }
    else
    {
        TrackedDrawArrays(primitiveType, startIndex, count);
    }

    glBindVertexArray(0);
}

void VertexArray::RenderInstanced(GLenum primitiveType, u32 count, u32 instanceCount) const
{
    if (!IsValid() || m_vertexBuffers.empty() || count == 0 || instanceCount == 0)
    {
        return;
    }

    ensureBuilt();

    glBindVertexArray(m_vao);

    if (!ValidateVertexCount(primitiveType, count))
    {
        LogWarning("Invalid vertex count %u for primitive type 0x%x\n", count, primitiveType);
    }

    if (m_indexBuffer && m_indexBuffer->IsValid())
    {
        const GLenum idxType = m_indexBuffer->GetIndexType();
        TrackedDrawElements(primitiveType, count, idxType, nullptr, instanceCount);
    }
    else
    {
        TrackedDrawArrays(primitiveType, 0, count, instanceCount);
    }

    glBindVertexArray(0);
}


void VertexArray::RenderInstanced(GLenum primitiveType, u32 count, u32 instanceCount, u32 startIndex) const
{
    if (!IsValid() || m_vertexBuffers.empty() || count == 0 || instanceCount == 0)
    {
        return;
    }

 
    ensureBuilt();

    glBindVertexArray(m_vao);

    if (!ValidateVertexCount(primitiveType, count))
    {
        LogWarning("Invalid vertex count %u for primitive type 0x%x\n", count, primitiveType);
    }

    if (m_indexBuffer && m_indexBuffer->IsValid())
    {
        const GLenum idxType = m_indexBuffer->GetIndexType();
        size_t indexSize = (idxType == GL_UNSIGNED_SHORT) ? sizeof(u16) : sizeof(u32);
        const void* offset = (const void*)(startIndex * indexSize);

        TrackedDrawElements(primitiveType, count, idxType, offset, instanceCount);
    }
    else
    {
        TrackedDrawArrays(primitiveType, startIndex, count, instanceCount);
    }

    glBindVertexArray(0);
}

void VertexArray::ResetStats()
{
    g_vertexDrawStats.Reset();
}

const VertexDrawStats &VertexArray::GetStats()
{
    return g_vertexDrawStats;
}