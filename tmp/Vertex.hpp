#pragma once
#include "Core.hpp"
#include <vector>
#include <map>
#include <cstdint>
#include "Opengl.hpp"


class VertexBuffer;
class IndexBuffer;
class VertexDeclaration;
class VertexArray;

 
enum VertexElementType
{
    VET_FLOAT1,
    VET_FLOAT2,
    VET_FLOAT3,
    VET_FLOAT4,
    VET_COLOR,  // 4 bytes RGBA
    VET_SHORT2, // 2 shorts
    VET_SHORT4, // 4 shorts
    VET_UBYTE4  // 4 unsigned bytes
};

enum VertexElementSemantic
{
    VES_POSITION = 0,
    VES_TEXCOORD,
    VES_COLOR,
    VES_NORMAL,
    VES_TANGENT,
    VES_BINORMAL,
    VES_BLEND_WEIGHTS,
    VES_BLEND_INDICES
};


struct VertexDrawStats
{
    u32 drawCalls         = 0;
    u32 indexedDrawCalls  = 0;
    u32 instancedCalls    = 0;
    u32 triangles         = 0;
    u32 lines             = 0;
    u32 points            = 0;
    u32 submittedVertices = 0;
    u32 submittedIndices  = 0;
    u32 instances         = 0;

    void Reset()
    {
        drawCalls = indexedDrawCalls = instancedCalls = 0;
        triangles = lines = points = 0;
        submittedVertices = submittedIndices = 0;
        instances = 0;
    }
};

// ============================================================================
// VERTEX ELEMENT
// ============================================================================

struct VertexElement
{
    u32 stream;
    u32 offset;
    VertexElementType type;
    VertexElementSemantic semantic;
    u32 index;
    u32 instanceDivisor; // 0 = per-vertex, 1+ = per-instance

    VertexElement();

    u32 GetSize() const;
    u32 GetComponentCount() const;
    u32 GetType() const;
    bool ShouldNormalize() const;
};

// ============================================================================
// VERTEX DECLARATION
// ============================================================================

class VertexDeclaration
{
private:
    std::vector<VertexElement> m_elements;
    std::map<u32, u32> m_streamSizes; // stream -> size
    mutable bool m_isDirty;

public:
    VertexDeclaration();

    void AddElement(u32 stream, u32 offset,
                    VertexElementType type, VertexElementSemantic semantic,
                    u32 index = 0, u32 divisor = 0);

    void Clear();
    u32 GetVertexSize(u32 stream = 0) const;
    const std::vector<VertexElement> &GetElements() const;
    const VertexElement *FindElement(VertexElementSemantic semantic, u32 index = 0) const;

    bool IsDirty() const { return m_isDirty; }
    void ClearDirty() const { m_isDirty = false; }
};

// ============================================================================
// BUFFER MANAGEMENT
// ============================================================================

class VertexBuffer
{
private:
    u32 m_vbo;
    u32 m_vertexSize;
    u32 m_vertexCount;
    bool m_isDynamic;

public:
    VertexBuffer(u32 vSize, u32 vCount, bool dynamic = false);
    ~VertexBuffer();

    // Delete copy
    VertexBuffer(const VertexBuffer &) = delete;
    VertexBuffer &operator=(const VertexBuffer &) = delete;

    // Move semantics
    VertexBuffer(VertexBuffer &&other) = delete;
    VertexBuffer &operator=(VertexBuffer &&other) = delete;

    void SetData(const void *data);
    void SetSubData(u32 offset, u32 size, const void *data);

    void Bind() const;

    // Getters
    u32 SetVertexSize() const { return m_vertexSize; }
    u32 SetVertexCount() const { return m_vertexCount; }
    u32 SetHandle() const { return m_vbo; }
    bool IsValid() const { return m_vbo != 0; }
};

class IndexBuffer
{
private:
    u32 m_ibo;
    u32 m_indexCount;
    bool m_is16Bit;
    bool m_isDynamic;

public:
    IndexBuffer(u32 iCount, bool dynamic = false, bool use16Bit = true);
    ~IndexBuffer();

    // Delete copy
    IndexBuffer(const IndexBuffer &) = delete;
    IndexBuffer &operator=(const IndexBuffer &) = delete;

    // Move semantics
    IndexBuffer(IndexBuffer &&other) = delete;
    IndexBuffer &operator=(IndexBuffer &&other) = delete;

    void SetData(const void *data);
    void SetSubData(u32 offset, u32 count, const void *data);

    void Bind() const;

    // Getters
    u32 GetIndexCount() const { return m_indexCount; }
    u32 GetIndexType() const;
    u32 GetHandle() const { return m_ibo; }
    bool IsValid() const { return m_ibo != 0; }
};

// ============================================================================
// VAO
// ============================================================================

class VertexArray
{
private:
    u32 m_vao;
    VertexDeclaration *m_vertexDeclaration;
    std::vector<VertexBuffer *> m_vertexBuffers;
    IndexBuffer *m_indexBuffer;
    mutable bool m_isBuilt;
    mutable bool m_needsRebuild;

    void ensureBuilt() const;

public:
    VertexArray();
    ~VertexArray();

    // Delete copy
    VertexArray(const VertexArray &) = delete;
    VertexArray &operator=(const VertexArray &) = delete;

    // Move semantics
    VertexArray(VertexArray &&other) = delete;
    VertexArray &operator=(VertexArray &&other) = delete;

    VertexDeclaration *GetVertexDeclaration() const { return m_vertexDeclaration; }

    VertexBuffer *AddVertexBuffer(u32 vSize, u32 vCount, bool dynamic = false);
    IndexBuffer *CreateIndexBuffer(u32 iCount, bool dynamic = false, bool use16Bit = true);

    void Release();

    void Build();
    void MarkDirty() { m_needsRebuild = true; }

    void Render(GLenum primitiveType, u32 count) const;
    void RenderInstanced(GLenum primitiveType, u32 count, u32 instanceCount) const;

    void Render(GLenum primitiveType, u32 count, u32 startIndex ) const;

    void RenderInstanced(GLenum primitiveType, u32 count, u32 instanceCount, u32 startIndex) const;

    static void ResetStats();
    static const VertexDrawStats &GetStats();

    bool IsValid() const { return m_vao != 0; }
};