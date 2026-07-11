#pragma once

#include <coregl/gl_config.hpp>

namespace scene
{

class ByteArray
{
public:
    ByteArray();
    ~ByteArray();

    ByteArray(const ByteArray&) = delete;
    ByteArray& operator=(const ByteArray&) = delete;

    ByteArray(ByteArray&& other) noexcept;
    ByteArray& operator=(ByteArray&& other) noexcept;

    void allocate(gl::u32 sz);
    void destroy();
    void resize(gl::u32 newSize);
    void clear();

    gl::u8* data() { return m_data; }
    const gl::u8* data() const { return m_data; }
    gl::u32 size() const { return m_size; }
    gl::u32 capacity() const { return m_capacity; }
    bool empty() const { return m_size == 0; }

    void setCursor(gl::u32 pos) { m_cursor = pos; }
    gl::u32 cursor() const { return m_cursor; }
    void resetCursor() { m_cursor = 0; }

    // Write
    void writeU8(gl::u8 value);
    void writeU16(gl::u16 value);
    void writeU32(gl::u32 value);
    void writeU64(gl::u64 value);
    void writeS8(gl::i8 value);
    void writeS16(gl::i16 value);
    void writeS32(gl::i32 value);
    void writeS64(gl::i64 value);
    void writeF32(gl::f32 value);
    void writeF64(gl::f64 value);
    void writeBool(bool value);
    void writeBytes(const gl::u8* src, gl::u32 count);
    void writeString(const char* str);

    // Read
    bool readU8(gl::u8& out);
    bool readU16(gl::u16& out);
    bool readU32(gl::u32& out);
    bool readU64(gl::u64& out);
    bool readS8(gl::i8& out);
    bool readS16(gl::i16& out);
    bool readS32(gl::i32& out);
    bool readS64(gl::i64& out);
    bool readF32(gl::f32& out);
    bool readF64(gl::f64& out);
    bool readBool(bool& out);
    bool readBytes(gl::u8* dst, gl::u32 count);
    bool readString(char* out, gl::u32 maxSize);

    // bulk reads for large arrays (mesh loading): one bounds check + one
    // memcpy for the whole array, instead of a function call and a bounds
    // check per element — the difference between milliseconds and seconds
    // once a mesh has hundreds of thousands of verts/indices.
    bool readF32Array(gl::f32* dst, gl::u32 count);
    bool readU32Array(gl::u32* dst, gl::u32 count);

    gl::u8 peekU8() const;

    bool seek(gl::u32 pos);
    bool skip(gl::u32 offset);
    void setBigEndian(bool big) { m_bigEndian = big; }

private:
    void ensureCapacity(gl::u32 required);

    gl::u8* m_data = nullptr;
    gl::u32 m_size = 0;
    gl::u32 m_capacity = 0;
    gl::u32 m_cursor = 0;
    bool m_bigEndian = false;
};

} // namespace scene