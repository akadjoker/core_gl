#pragma once

#include "Types.hpp"
#include <string>
#include <vector>


#pragma once
#include <cstddef>
#include <string>
#include <cstring>
#include "Types.hpp"

enum class SeekOrigin : int
{
    Begin = 0,
    Current = 1,
    End = 2
};

class Stream
{
public:
    virtual ~Stream() = default;

    // Core I/O (write-only para converter)
    virtual size_t Write(const void* buffer, size_t size) = 0;
    virtual bool Seek(long offset, SeekOrigin origin = SeekOrigin::Begin) = 0;
    virtual long Tell() const = 0;
    virtual bool IsOpen() const = 0;
    virtual void Close() = 0;

    // Write helpers
    void WriteByte(u8 value);
    void WriteBool(bool value);
    void WriteShort(s16 value);
    void WriteUShort(u16 value);
    void WriteInt(s32 value);
    void WriteUInt(u32 value);
    void WriteLong(s64 value);
    void WriteULong(u64 value);
    void WriteFloat(f32 value);
    void WriteDouble(f64 value);
    void WriteCString(const std::string& str);

    void SetBigEndian(bool bigEndian) { m_bigEndian = bigEndian; }
    bool IsBigEndian() const { return m_bigEndian; }

protected:
    bool m_bigEndian = false;

    u16 SwapU16(u16 value) const;
    u32 SwapU32(u32 value) const;
    u64 SwapU64(u64 value) const;

    void WriteU16(u16 value);
    void WriteU32(u32 value);
    void WriteU64(u64 value);
};

class FileStream : public Stream
{
public:
    FileStream();
    explicit FileStream(const std::string& filename, const std::string& mode = "wb");
    virtual ~FileStream();

    bool Open(const std::string& filename, const std::string& mode = "wb");
    virtual void Close() override;

    virtual size_t Write(const void* buffer, size_t size) override;
    virtual bool Seek(long offset, SeekOrigin origin = SeekOrigin::Begin) override;
    virtual long Tell() const override;
    virtual bool IsOpen() const override;

    const std::string& GetFilename() const { return m_filename; }

private:
    FILE* m_file;
    std::string m_filename;
};