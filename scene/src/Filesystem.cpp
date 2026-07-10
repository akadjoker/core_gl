#include "scene/Filesystem.hpp"
#include "scene/IO.hpp"

#include <cstdio>
#include <cstring>

namespace fs
{

static void normalizePath(char* buf, gl::u32 bufSize, const char* src)
{
    if (!buf || bufSize == 0) return;
    gl::u32 i = 0;
    for (; i + 1 < bufSize && src[i] != '\0'; ++i)
    {
        char c = src[i];
        buf[i] = (c == '\\') ? '/' : c;
    }
    buf[i] = '\0';

    while (i > 0 && (buf[i - 1] == '/' || buf[i - 1] == '\\'))
    {
        buf[--i] = '\0';
    }
}

Filesystem::Filesystem() = default;
Filesystem::~Filesystem() = default;

void Filesystem::addFolder(const char* path)
{
    if (!path || m_pathCount >= MAX_PATHS) return;

    PathEntry& entry = m_paths[m_pathCount];
    entry.type = PathEntry::FOLDER;
    normalizePath(entry.path, sizeof(entry.path), path);
    m_pathCount++;
}

void Filesystem::addArchive(const char* path)
{
    if (!path || m_pathCount >= MAX_PATHS) return;

    PathEntry& entry = m_paths[m_pathCount];
    entry.type = PathEntry::ARCHIVE;
    normalizePath(entry.path, sizeof(entry.path), path);
    m_pathCount++;
}

void Filesystem::clearPaths()
{
    m_pathCount = 0;
}

bool Filesystem::exists(const char* filename)
{
    if (!filename) return false;

    io::FileInterface* iface = io::getFileInterface();
    if (!iface) return false;

    if (iface->exists(filename)) return true;

    char resolved[512];
    return resolvePath(filename, resolved, sizeof(resolved));
}

bool Filesystem::resolvePath(const char* filename, char* outPath, gl::u32 outSize)
{
    if (!filename || !outPath || outSize == 0) return false;

    io::FileInterface* iface = io::getFileInterface();
    if (!iface) return false;

    if (iface->exists(filename))
    {
        std::strncpy(outPath, filename, outSize - 1);
        outPath[outSize - 1] = '\0';
        return true;
    }

    for (gl::u32 i = 0; i < m_pathCount; ++i)
    {
        const PathEntry& entry = m_paths[i];
        if (entry.type != PathEntry::FOLDER) continue;

        int n = std::snprintf(outPath, outSize, "%s/%s", entry.path, filename);
        if (n <= 0 || n >= (int)outSize) continue;

        if (iface->exists(outPath)) return true;
    }

    outPath[0] = '\0';
    return false;
}

static bool readFileViaIO(const char* path, scene::ByteArray& out)
{
    io::FileInterface* iface = io::getFileInterface();
    if (!iface) return false;

    io::FileHandle* handle = iface->open(path);
    if (!handle) return false;

    gl::i64 sz = iface->size(handle);
    if (sz < 0)
    {
        iface->close(handle);
        return false;
    }

    out.allocate((gl::u32)sz);
    if (out.size() != (gl::u32)sz)
    {
        iface->close(handle);
        return false;
    }

    iface->seek(handle, 0, io::SeekMode::Begin);
    gl::i64 rd = iface->read(handle, out.data(), (gl::u64)sz);
    iface->close(handle);

    out.resetCursor();
    return rd == sz;
}

bool Filesystem::readFile(const char* filename, scene::ByteArray& out)
{
    if (!filename) return false;

    char resolved[512];
    if (resolvePath(filename, resolved, sizeof(resolved)))
    {
        return readFileViaIO(resolved, out);
    }

    return false;
}

bool Filesystem::readText(const char* filename, scene::ByteArray& out)
{
    if (!readFile(filename, out)) return false;

    gl::u32 textLen = out.size();
    out.resize(textLen + 1);
    out.data()[textLen] = '\0';
    out.resetCursor();
    return true;
}

const PathEntry* Filesystem::getPath(gl::u32 index) const
{
    if (index >= m_pathCount) return nullptr;
    return &m_paths[index];
}

Filesystem& getFilesystem()
{
    static Filesystem instance;
    // first use registers the platform's default file backend, so plain
    // desktop apps read files without any setup call
    if (!io::getFileInterface()) io::registerDefaultDesktop();
    return instance;
}

} // namespace fs