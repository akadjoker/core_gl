#pragma once

#include <coregl/gl_config.hpp>

namespace io
{

enum class SeekMode : gl::u8
{
    Begin = 0,
    Current = 1,
    End = 2
};

struct FileHandle;

class FileInterface
{
public:
    virtual ~FileInterface() = default;

    virtual bool exists(const char* path) = 0;

    virtual FileHandle* open(const char* path) = 0;
    virtual void close(FileHandle* handle) = 0;

    virtual gl::i64 read(FileHandle* handle, void* dst, gl::u64 size) = 0;
    virtual gl::i64 seek(FileHandle* handle, gl::i64 offset, SeekMode mode) = 0;
    virtual gl::i64 tell(FileHandle* handle) = 0;
    virtual gl::i64 size(FileHandle* handle) = 0;
};

void registerFileInterface(FileInterface* iface);
FileInterface* getFileInterface();

void registerDefaultDesktop();

} // namespace io