#pragma once

#include "scene/ByteArray.hpp"
#include <coregl/gl_config.hpp>

namespace fs
{

struct PathEntry
{
    enum Type : unsigned char
    {
        FOLDER,
        ARCHIVE
    };
    Type type;
    char path[256];
};

class Filesystem
{
public:
    Filesystem();
    ~Filesystem();

    void addFolder(const char* path);
    void addArchive(const char* path);
    void clearPaths();

    bool resolvePath(const char* filename, char* outPath, gl::u32 outSize);
    bool exists(const char* filename);

    bool readFile(const char* filename, scene::ByteArray& out);
    bool readText(const char* filename, scene::ByteArray& out);

    gl::u32 pathCount() const { return m_pathCount; }
    const PathEntry* getPath(gl::u32 index) const;

private:
    static constexpr gl::u32 MAX_PATHS = 32;
    PathEntry m_paths[MAX_PATHS];
    gl::u32 m_pathCount = 0;
};

Filesystem& getFilesystem();

} // namespace fs