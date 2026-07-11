#include "scene/ByteArray.hpp"
#include <algorithm>
#include <cstring>
#include <cstdlib>

namespace scene
{

ByteArray::ByteArray() = default;

ByteArray::~ByteArray() { destroy(); }

ByteArray::ByteArray(ByteArray&& other) noexcept
    : m_data(other.m_data),
      m_size(other.m_size),
      m_capacity(other.m_capacity),
      m_cursor(other.m_cursor),
      m_bigEndian(other.m_bigEndian)
{
    other.m_data = nullptr;
    other.m_size = 0;
    other.m_capacity = 0;
    other.m_cursor = 0;
}

ByteArray& ByteArray::operator=(ByteArray&& other) noexcept
{
    if (this != &other)
    {
        destroy();
        m_data = other.m_data;
        m_size = other.m_size;
        m_capacity = other.m_capacity;
        m_cursor = other.m_cursor;
        m_bigEndian = other.m_bigEndian;
        other.m_data = nullptr;
        other.m_size = 0;
        other.m_capacity = 0;
        other.m_cursor = 0;
    }
    return *this;
}

void ByteArray::allocate(gl::u32 sz)
{
    if (sz == 0)
    {
        destroy();
        return;
    }
    if (sz > m_capacity)
    {
        destroy();
        m_data = (gl::u8*)std::malloc(sz);
        if (m_data)
        {
            m_capacity = sz;
            m_size = sz;
        }
    }
    else
    {
        m_size = sz;
    }
    m_cursor = 0;
}

void ByteArray::destroy()
{
    if (m_data)
    {
        std::free(m_data);
        m_data = nullptr;
    }
    m_size = 0;
    m_capacity = 0;
    m_cursor = 0;
}

void ByteArray::resize(gl::u32 newSize)
{
    if (newSize == 0)
    {
        clear();
        return;
    }
    if (newSize > m_capacity)
    {
        ensureCapacity(newSize);
    }
    m_size = newSize;
}

void ByteArray::clear()
{
    m_size = 0;
    m_cursor = 0;
}

void ByteArray::ensureCapacity(gl::u32 required)
{
    if (required <= m_capacity) return;

    gl::u32 newCap = m_capacity ? m_capacity : 64;
    while (newCap < required) newCap *= 2;

    gl::u8* newData = (gl::u8*)std::malloc(newCap);
    if (!newData) return;

    if (m_data && m_size > 0)
        std::memcpy(newData, m_data, m_size);

    std::free(m_data);
    m_data = newData;
    m_capacity = newCap;
}

// ── Write ──────────────────────────────────────────────

void ByteArray::writeBytes(const gl::u8* src, gl::u32 count)
{
    if (!src || count == 0) return;
    ensureCapacity(m_cursor + count);
    if (m_cursor + count > m_size) m_size = m_cursor + count;
    std::memcpy(m_data + m_cursor, src, count);
    m_cursor += count;
}

void ByteArray::writeU8(gl::u8 v) { writeBytes(&v, 1); }
void ByteArray::writeS8(gl::i8 v) { writeBytes((const gl::u8*)&v, 1); }
void ByteArray::writeBool(bool v) { writeU8(v ? 1 : 0); }

void ByteArray::writeU16(gl::u16 v)
{
    if (m_bigEndian)
    {
        gl::u8 b[2] = {gl::u8(v >> 8), gl::u8(v & 0xFF)};
        writeBytes(b, 2);
    }
    else
    {
        writeBytes((const gl::u8*)&v, 2);
    }
}

void ByteArray::writeU32(gl::u32 v)
{
    if (m_bigEndian)
    {
        gl::u8 b[4] = {gl::u8(v >> 24), gl::u8(v >> 16), gl::u8(v >> 8), gl::u8(v & 0xFF)};
        writeBytes(b, 4);
    }
    else
    {
        writeBytes((const gl::u8*)&v, 4);
    }
}

void ByteArray::writeU64(gl::u64 v)
{
    if (m_bigEndian)
    {
        gl::u8 b[8];
        for (int i = 0; i < 8; ++i)
            b[i] = gl::u8(v >> (56 - 8 * i));
        writeBytes(b, 8);
    }
    else
    {
        writeBytes((const gl::u8*)&v, 8);
    }
}

void ByteArray::writeS16(gl::i16 v) { writeU16((gl::u16)v); }
void ByteArray::writeS32(gl::i32 v) { writeU32((gl::u32)v); }
void ByteArray::writeS64(gl::i64 v) { writeU64((gl::u64)v); }

void ByteArray::writeF32(gl::f32 v)
{
    gl::u32 bits;
    std::memcpy(&bits, &v, sizeof(bits));
    writeU32(bits);
}

void ByteArray::writeF64(gl::f64 v)
{
    gl::u64 bits;
    std::memcpy(&bits, &v, sizeof(bits));
    writeU64(bits);
}

void ByteArray::writeString(const char* str)
{
    if (!str)
    {
        writeU8(0);
        return;
    }
    gl::u32 len = (gl::u32)std::strlen(str);
    writeU32(len);
    writeBytes((const gl::u8*)str, len);
}

// ── Read ───────────────────────────────────────────────

bool ByteArray::readBytes(gl::u8* dst, gl::u32 count)
{
    if (!dst || count == 0) return true;
    if (m_cursor + count > m_size) return false;
    std::memcpy(dst, m_data + m_cursor, count);
    m_cursor += count;
    return true;
}

bool ByteArray::readU8(gl::u8& out)
{
    return readBytes(&out, 1);
}

bool ByteArray::readU16(gl::u16& out)
{
    gl::u8 b[2];
    if (!readBytes(b, 2)) return false;
    if (m_bigEndian)
        out = (gl::u16(b[0]) << 8) | b[1];
    else
        std::memcpy(&out, b, 2);
    return true;
}

bool ByteArray::readU32(gl::u32& out)
{
    gl::u8 b[4];
    if (!readBytes(b, 4)) return false;
    if (m_bigEndian)
        out = (gl::u32(b[0]) << 24) | (gl::u32(b[1]) << 16) | (gl::u32(b[2]) << 8) | b[3];
    else
        std::memcpy(&out, b, 4);
    return true;
}

bool ByteArray::readU64(gl::u64& out)
{
    gl::u8 b[8];
    if (!readBytes(b, 8)) return false;
    if (m_bigEndian)
    {
        gl::u64 v = 0;
        for (int i = 0; i < 8; ++i)
            v = (v << 8) | b[i];
        out = v;
    }
    else
    {
        std::memcpy(&out, b, 8);
    }
    return true;
}

bool ByteArray::readS8(gl::i8& out) { gl::u8 u; if (!readU8(u)) return false; out = (gl::i8)u; return true; }
bool ByteArray::readS16(gl::i16& out) { gl::u16 u; if (!readU16(u)) return false; out = (gl::i16)u; return true; }
bool ByteArray::readS32(gl::i32& out) { gl::u32 u; if (!readU32(u)) return false; out = (gl::i32)u; return true; }
bool ByteArray::readS64(gl::i64& out) { gl::u64 u; if (!readU64(u)) return false; out = (gl::i64)u; return true; }

bool ByteArray::readBool(bool& out)
{
    gl::u8 u;
    if (!readU8(u)) return false;
    out = (u != 0);
    return true;
}

bool ByteArray::readF32(gl::f32& out)
{
    gl::u32 bits;
    if (!readU32(bits)) return false;
    std::memcpy(&out, &bits, sizeof(out));
    return true;
}

// one bounds check, one memcpy for the whole array; endian swap (rare —
// the exporter always writes little-endian) is a single pass afterward
bool ByteArray::readF32Array(gl::f32* dst, gl::u32 count)
{
    static_assert(sizeof(gl::f32) == 4, "readF32Array assumes 4-byte float");
    if (!readBytes(reinterpret_cast<gl::u8*>(dst), count * 4)) return false;
    if (m_bigEndian)
    {
        for (gl::u32 i = 0; i < count; ++i)
        {
            gl::u8* b = reinterpret_cast<gl::u8*>(&dst[i]);
            std::swap(b[0], b[3]);
            std::swap(b[1], b[2]);
        }
    }
    return true;
}

bool ByteArray::readU32Array(gl::u32* dst, gl::u32 count)
{
    if (!readBytes(reinterpret_cast<gl::u8*>(dst), count * 4)) return false;
    if (m_bigEndian)
    {
        for (gl::u32 i = 0; i < count; ++i)
        {
            gl::u8* b = reinterpret_cast<gl::u8*>(&dst[i]);
            std::swap(b[0], b[3]);
            std::swap(b[1], b[2]);
        }
    }
    return true;
}

bool ByteArray::readF64(gl::f64& out)
{
    gl::u64 bits;
    if (!readU64(bits)) return false;
    std::memcpy(&out, &bits, sizeof(out));
    return true;
}

bool ByteArray::readString(char* out, gl::u32 maxSize)
{
    gl::u32 len;
    if (!readU32(len)) return false;
    if (len >= maxSize) return false;
    if (!readBytes((gl::u8*)out, len)) return false;
    out[len] = '\0';
    return true;
}

// ── Utility ────────────────────────────────────────────

gl::u8 ByteArray::peekU8() const
{
    if (!m_data || m_cursor >= m_size) return 0;
    return m_data[m_cursor];
}

bool ByteArray::seek(gl::u32 pos)
{
    if (pos > m_size) return false;
    m_cursor = pos;
    return true;
}

bool ByteArray::skip(gl::u32 offset)
{
    if (m_cursor + offset > m_size) return false;
    m_cursor += offset;
    return true;
}

} // namespace scene