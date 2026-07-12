 
#include "Stream.hpp"
 

u16 Stream::SwapU16(u16 value) const
{
    return ((value & 0xFF00) >> 8) | ((value & 0x00FF) << 8);
}

u32 Stream::SwapU32(u32 value) const
{
    return ((value & 0xFF000000) >> 24) |
           ((value & 0x00FF0000) >> 8) |
           ((value & 0x0000FF00) << 8) |
           ((value & 0x000000FF) << 24);
}

u64 Stream::SwapU64(u64 value) const
{
    return ((value & 0xFF00000000000000ULL) >> 56) |
           ((value & 0x00FF000000000000ULL) >> 40) |
           ((value & 0x0000FF0000000000ULL) >> 24) |
           ((value & 0x000000FF00000000ULL) >> 8) |
           ((value & 0x00000000FF000000ULL) << 8) |
           ((value & 0x0000000000FF0000ULL) << 24) |
           ((value & 0x000000000000FF00ULL) << 40) |
           ((value & 0x00000000000000FFULL) << 56);
}

void Stream::WriteU16(u16 value)
{
    // Little-endian por default (x86/x64)
    if (m_bigEndian)
        value = SwapU16(value);
    Write(&value, sizeof(u16));
}

void Stream::WriteU32(u32 value)
{
    if (m_bigEndian)
        value = SwapU32(value);
    Write(&value, sizeof(u32));
}

void Stream::WriteU64(u64 value)
{
    if (m_bigEndian)
        value = SwapU64(value);
    Write(&value, sizeof(u64));
}

void Stream::WriteByte(u8 value)
{
    Write(&value, 1);
}

void Stream::WriteBool(bool value)
{
    WriteByte(value ? 1 : 0);
}

void Stream::WriteShort(s16 value)
{
    WriteU16(static_cast<u16>(value));
}

void Stream::WriteUShort(u16 value)
{
    WriteU16(value);
}

void Stream::WriteInt(s32 value)
{
    WriteU32(static_cast<u32>(value));
}

void Stream::WriteUInt(u32 value)
{
    WriteU32(value);
}

void Stream::WriteLong(s64 value)
{
    WriteU64(static_cast<u64>(value));
}

void Stream::WriteULong(u64 value)
{
    WriteU64(value);
}

void Stream::WriteFloat(f32 value)
{
    u32 temp;
    memcpy(&temp, &value, sizeof(f32));
    WriteU32(temp);
}

void Stream::WriteDouble(f64 value)
{
    u64 temp;
    memcpy(&temp, &value, sizeof(f64));
    WriteU64(temp);
}

void Stream::WriteCString(const std::string& str)
{
   if (!str.empty())
    {
        Write(str.c_str(), str.length());
    }
    WriteByte(0); // Null terminator
}


FileStream::FileStream()
    : m_file(nullptr)
{
}

FileStream::FileStream(const std::string& filename, const std::string& mode)
    : m_file(nullptr)
{
    Open(filename, mode);
}

FileStream::~FileStream()
{
    Close();
}

bool FileStream::Open(const std::string& filename, const std::string& mode)
{
    Close();
    m_filename = filename;

#ifdef _MSC_VER
    // Windows: usa fopen_s
    errno_t err = fopen_s(&m_file, filename.c_str(), mode.c_str());
    if (err != 0)
    {
        m_file = nullptr;
        return false;
    }
#else
    // Linux/Mac: fopen normal
    m_file = fopen(filename.c_str(), mode.c_str());
    if (!m_file)
        return false;
#endif

    return true;
}

void FileStream::Close()
{
    if (m_file)
    {
        fclose(m_file);
        m_file = nullptr;
    }
}

size_t FileStream::Write(const void* buffer, size_t size)
{
    if (!m_file) return 0;
    return fwrite(buffer, 1, size, m_file);
}

bool FileStream::Seek(long offset, SeekOrigin origin)
{
    if (!m_file) return false;
    
    int whence = SEEK_SET;
    switch (origin)
    {
    case SeekOrigin::Begin:   whence = SEEK_SET; break;
    case SeekOrigin::Current: whence = SEEK_CUR; break;
    case SeekOrigin::End:     whence = SEEK_END; break;
    }
    
    return fseek(m_file, offset, whence) == 0;
}

long FileStream::Tell() const
{
    if (!m_file) return -1;
    return ftell(m_file);
}

bool FileStream::IsOpen() const
{
    return m_file != nullptr;
}
