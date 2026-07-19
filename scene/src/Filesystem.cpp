#include "scene/Filesystem.hpp"
#include "scene/IO.hpp"

#include <cstdio>
#include <cstring>

// Vendored miniz (scene/src/miniz.h, public domain) — declarations only
// here; the actual implementation is compiled once in miniz_impl.cpp.
#define MINIZ_HEADER_FILE_ONLY
#include "miniz.h"

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

Filesystem::~Filesystem()
{
    clearPaths();
}

void Filesystem::addFolder(const char* path)
{
    if (!path || m_pathCount >= MAX_PATHS) return;

    PathEntry& entry = m_paths[m_pathCount];
    entry.type = PathEntry::FOLDER;
    normalizePath(entry.path, sizeof(entry.path), path);
    m_archiveHandles[m_pathCount] = nullptr;
    m_pathCount++;
}

// Mounts a .pk3/.zip as a searchable path, same idea as Quake's own pak
// search order — files inside are looked up by their archive-relative name
// (e.g. "textures/base_floor/clang.jpg"), exactly like a real folder.
void Filesystem::addArchive(const char* path)
{
    if (!path || m_pathCount >= MAX_PATHS) return;

    mz_zip_archive* zip = new mz_zip_archive();
    std::memset(zip, 0, sizeof(mz_zip_archive));
    if (!mz_zip_reader_init_file(zip, path, 0))
    {
        delete zip;
        return; // not a valid zip/pk3 — silently skip, same as a missing folder
    }

    PathEntry& entry = m_paths[m_pathCount];
    entry.type = PathEntry::ARCHIVE;
    normalizePath(entry.path, sizeof(entry.path), path);
    m_archiveHandles[m_pathCount] = zip;
    m_pathCount++;
}

void Filesystem::clearPaths()
{
    for (gl::u32 i = 0; i < m_pathCount; ++i)
    {
        if (m_paths[i].type != PathEntry::ARCHIVE) continue;
        mz_zip_archive* zip = static_cast<mz_zip_archive*>(m_archiveHandles[i]);
        if (!zip) continue;
        mz_zip_reader_end(zip);
        delete zip;
        m_archiveHandles[i] = nullptr;
    }
    m_pathCount = 0;
}

bool Filesystem::exists(const char* filename)
{
    if (!filename) return false;

    io::FileInterface* iface = io::getFileInterface();
    if (!iface) return false;

    if (iface->exists(filename)) return true;

    char resolved[512];
    if (resolvePath(filename, resolved, sizeof(resolved))) return true;

    for (gl::u32 i = 0; i < m_pathCount; ++i)
    {
        if (m_paths[i].type != PathEntry::ARCHIVE) continue;
        mz_zip_archive* zip = static_cast<mz_zip_archive*>(m_archiveHandles[i]);
        if (!zip) continue;
        if (mz_zip_reader_locate_file(zip, filename, nullptr, 0) >= 0) return true;
    }
    return false;
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

    // Not a real file anywhere on disk — try each mounted .pk3/.zip in
    // registration order (same "first match wins" convention as folders).
    for (gl::u32 i = 0; i < m_pathCount; ++i)
    {
        if (m_paths[i].type != PathEntry::ARCHIVE) continue;
        mz_zip_archive* zip = static_cast<mz_zip_archive*>(m_archiveHandles[i]);
        if (!zip) continue;

        size_t extractedSize = 0;
        void* data = mz_zip_reader_extract_file_to_heap(zip, filename, &extractedSize, 0);
        if (!data) continue;

        out.allocate((gl::u32)extractedSize);
        if (out.size() == (gl::u32)extractedSize)
            std::memcpy(out.data(), data, extractedSize);
        mz_free(data);
        out.resetCursor();
        return out.size() == (gl::u32)extractedSize;
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