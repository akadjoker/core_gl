#pragma once

#include "gl_types.hpp"

namespace gl
{

class Buffer;

class VertexArray
{
    u32 id = 0;
    u32 attribIndex = 0;                                 // next free attribute index
    VertexAttribType indexType = VertexAttribType::UINT; // IBO index type (for DrawIndexed)

public:
    VertexArray();
    ~VertexArray();
    VertexArray(VertexArray&& other) noexcept;
    VertexArray& operator=(VertexArray&& other) noexcept;
    VertexArray(const VertexArray&) = delete;
    VertexArray& operator=(const VertexArray&) = delete;

 
    void Release();

    void Bind();
    void Unbind();

    // Registers a VBO with the given layout; stride in bytes.
    // Attribute indices are assigned sequentially (0, 1, 2, ...).
    void AddVertexBuffer(const Buffer& vbo, const VertexAttrib* attribs, u32 attribCount,
                         u32 stride);

    // Same, but with divisor = 1 on every attribute (per-instance)
    void AddInstanceBuffer(const Buffer& vbo, const VertexAttrib* attribs, u32 attribCount,
                           u32 stride);

    // indexType: USHORT or UINT
    void SetIndexBuffer(const Buffer& ibo, VertexAttribType indexType = VertexAttribType::UINT);

    VertexAttribType GetIndexType() const { return indexType; }
    u32 GetHandle() const { return id; }
    bool IsValid() const { return id != 0; }
};

} // namespace gl
