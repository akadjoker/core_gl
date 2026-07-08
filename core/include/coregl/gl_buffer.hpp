#pragma once

#include "gl_types.hpp"
#include <cstddef>

namespace gl
{

class Buffer
{
    u32 id = 0;
    BufferType type = BufferType::UNKNOWN;
    UsageType usage = UsageType::STATIC_DRAW;
    size_t byteSize = 0;

public:
    Buffer();
    ~Buffer();
    Buffer(Buffer&& other) noexcept;
    Buffer& operator=(Buffer&& other) noexcept;
    Buffer(const Buffer&) = delete;
    Buffer& operator=(const Buffer&) = delete;

 
    void Release();

    // Creates the buffer and allocates byteSize bytes; data may be nullptr (reserve only)
    void Allocate(BufferType type, const void* data, size_t byteSize, UsageType usage);

    // Updates a region (buffer must be allocated)
    void Upload(const void* data, size_t byteSize, size_t offset = 0);

    // Reads back into memory (desktop / ES via map)
    void Download(void* out, size_t byteSize, size_t offset = 0);

    void Bind();
    void Unbind();

    // Binds to an indexed binding point (SSBO / UBO)
    void BindBase(u32 index);

    size_t GetSize() const { return byteSize; }
    u32 GetHandle() const { return id; }
    BufferType GetType() const { return type; }
    bool IsValid() const { return id != 0; }
};

} // namespace gl
