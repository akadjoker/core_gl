#include "scene/IO.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/stat.h>

namespace io
{

static FileInterface* g_iface = nullptr;

void registerFileInterface(FileInterface* iface)
{
    g_iface = iface;
}

FileInterface* getFileInterface()
{
    return g_iface;
}

class DesktopFileInterface : public FileInterface
{
public:
    bool exists(const char* path) override
    {
        struct stat st;
        return (stat(path, &st) == 0);
    }

    FileHandle* open(const char* path) override
    {
        std::FILE* f = std::fopen(path, "rb");
        return (FileHandle*)f;
    }

    void close(FileHandle* handle) override
    {
        if (handle) std::fclose((std::FILE*)handle);
    }

    gl::i64 read(FileHandle* handle, void* dst, gl::u64 size) override
    {
        if (!handle) return -1;
        return (gl::i64)std::fread(dst, 1, (size_t)size, (std::FILE*)handle);
    }

    gl::i64 seek(FileHandle* handle, gl::i64 offset, SeekMode mode) override
    {
        if (!handle) return -1;
        int whence =
            (mode == SeekMode::Begin)    ? SEEK_SET :
            (mode == SeekMode::Current)  ? SEEK_CUR :
                                           SEEK_END;
        return (std::fseek((std::FILE*)handle, (long)offset, whence) == 0) ? 0 : -1;
    }

    gl::i64 tell(FileHandle* handle) override
    {
        if (!handle) return -1;
        return (gl::i64)std::ftell((std::FILE*)handle);
    }

    gl::i64 size(FileHandle* handle) override
    {
        if (!handle) return -1;
        std::FILE* f = (std::FILE*)handle;
        long cur = std::ftell(f);
        if (cur < 0) return -1;
        if (std::fseek(f, 0, SEEK_END) != 0) return -1;
        long sz = std::ftell(f);
        std::fseek(f, cur, SEEK_SET);
        return (gl::i64)sz;
    }
};

static DesktopFileInterface g_desktop;

void registerDefaultDesktop()
{
    registerFileInterface(&g_desktop);
}

} // namespace io