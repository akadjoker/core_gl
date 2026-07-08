
#include "pch.h"
#include "BinaryStream.hpp"
#include "Utils.hpp"
#include <algorithm>
#include <cctype>
#include <fstream>

#define MAX_TEXT_BUFFER_LENGTH 512

static void LogMessage(int level, const char *msg, va_list args)
{
    time_t rawTime;
    struct tm *timeInfo;
    char timeBuffer[80];

    time(&rawTime);
    timeInfo = localtime(&rawTime);

    strftime(timeBuffer, sizeof(timeBuffer), "[%H:%M:%S]", timeInfo);

    char buffer[1024];
    vsnprintf(buffer, sizeof(buffer), msg, args);

    switch (level)
    {
    case 0:
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "%s%s", timeBuffer, buffer);
        break;
    case 1:
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "%s%s", timeBuffer, buffer);
        break;
    case 2:
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "%s%s", timeBuffer, buffer);
        break;
    }
}

void LogError(const char *msg, ...)
{

    va_list args;
    va_start(args, msg);
    LogMessage(1, msg, args);
    va_end(args);
}

void LogWarning(const char *msg, ...)
{

    va_list args;
    va_start(args, msg);
    LogMessage(2, msg, args);
    va_end(args);
}

void LogInfo(const char *msg, ...)
{
    va_list args;
    va_start(args, msg);
    LogMessage(0, msg, args);
    va_end(args);
}

bool FileExists(const std::string &path)
{
    if (path.empty())
        return false;

    SDL_RWops *rw = SDL_RWFromFile(path.c_str(), "rb");
    if (!rw)
        return false;
    SDL_RWclose(rw);
    return true;
}

bool PathIsAbsolute(const std::string &path)
{
    if (path.empty())
        return false;
    if (path[0] == '/' || path[0] == '\\')
        return true;
    return path.size() > 1 && path[1] == ':';
}

std::string PathDirectory(const std::string &path)
{
    const size_t a = path.find_last_of('/');
    const size_t b = path.find_last_of('\\');
    size_t pos = std::string::npos;

    if (a == std::string::npos)
        pos = b;
    else if (b == std::string::npos)
        pos = a;
    else
        pos = std::max(a, b);

    if (pos == std::string::npos)
        return std::string();
    return path.substr(0, pos);
}

std::string PathJoin(const std::string &dir, const std::string &file)
{
    if (dir.empty())
        return file;
    if (file.empty())
        return dir;

    const char last = dir[dir.size() - 1];
    if (last == '/' || last == '\\')
        return dir + file;
    return dir + "/" + file;
}

std::string PathFilename(const std::string &path)
{
    const size_t a = path.find_last_of('/');
    const size_t b = path.find_last_of('\\');
    size_t pos = std::string::npos;

    if (a == std::string::npos)
        pos = b;
    else if (b == std::string::npos)
        pos = a;
    else
        pos = std::max(a, b);

    if (pos == std::string::npos)
        return path;
    return path.substr(pos + 1);
}

std::string PathStem(const std::string &path)
{
    const std::string file = PathFilename(path);
    const size_t dot = file.find_last_of('.');
    if (dot == std::string::npos || dot == 0)
        return file;
    return file.substr(0, dot);
}

bool HasExtension(const char *fileName, const char *extension)
{
    if (!fileName || !extension)
        return false;

    const std::string path(fileName);
    const std::string suffix(extension);
    if (path.length() < suffix.length())
        return false;

    return path.compare(path.length() - suffix.length(), suffix.length(), suffix) == 0;
}

std::string ResolveTexturePath(const std::string &baseDir, const std::string &shaderName)
{
    if (shaderName.empty())
        return std::string();
    if (PathIsAbsolute(shaderName) && FileExists(shaderName))
        return shaderName;

    std::string rel = shaderName;
    std::replace(rel.begin(), rel.end(), '\\', '/');
    while (!rel.empty() && rel.front() == '/')
        rel.erase(rel.begin());

    if (FileExists(rel))
        return rel;

    const std::string joined = PathJoin(baseDir, rel);
    if (FileExists(joined))
        return joined;

    const std::string fileOnly = PathFilename(rel);
    if (!fileOnly.empty())
    {
        const std::string joinedFile = PathJoin(baseDir, fileOnly);
        if (FileExists(joinedFile))
            return joinedFile;
    }

    const char *exts[] = {
        ".png", ".jpg", ".jpeg", ".bmp", ".tga", ".webp"};

    for (size_t i = 0; i < sizeof(exts) / sizeof(exts[0]); ++i)
    {
        const std::string joinedExt = PathJoin(baseDir, rel + exts[i]);
        if (FileExists(joinedExt))
            return joinedExt;

        if (!fileOnly.empty())
        {
            const std::string joinedFileExt = PathJoin(baseDir, fileOnly + exts[i]);
            if (FileExists(joinedFileExt))
                return joinedFileExt;
        }
    }

    // Search common texture subdirectories
    const char *subDirs[] = {"textures", "tex", "Textures", "Tex", "../textures", "../Textures"};
    for (const char *sub : subDirs)
    {
        const std::string subBase = PathJoin(baseDir, sub);
        if (!fileOnly.empty())
        {
            const std::string subJoined = PathJoin(subBase, fileOnly);
            if (FileExists(subJoined))
                return subJoined;
            for (size_t i = 0; i < sizeof(exts) / sizeof(exts[0]); ++i)
            {
                const std::string subExt = PathJoin(subBase, fileOnly + exts[i]);
                if (FileExists(subExt))
                    return subExt;
            }
        }
    }

    return std::string();
}

std::string TrimString(std::string value)
{
    const auto is_space = [](unsigned char c) { return std::isspace(c) != 0; };
    value.erase(value.begin(), std::find_if(value.begin(), value.end(),
                                            [&](char c) { return !is_space((unsigned char)c); }));
    value.erase(std::find_if(value.rbegin(), value.rend(),
                             [&](char c) { return !is_space((unsigned char)c); }).base(),
                value.end());
    return value;
}

std::string LowerString(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return (char)std::tolower(c); });
    return value;
}

std::string ReadFixedString(BinaryStream &s, size_t size)
{
    std::string out(size, '\0');
    s.readRaw(out.data(), size);
    const size_t end = out.find('\0');
    if (end != std::string::npos)
        out.resize(end);
    return out;
}

bool ReadFileBytes(const std::string &path, std::vector<uint8_t> &out)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return false;

    in.seekg(0, std::ios::end);
    const std::streamoff sz = in.tellg();
    if (sz <= 0)
        return false;

    out.resize(static_cast<size_t>(sz));
    in.seekg(0, std::ios::beg);
    in.read(reinterpret_cast<char *>(out.data()), sz);
    return in.good() || in.eof();
}

bool IsFileExtension(const char *fileName, const char *ext)
{
    bool result = false;

    const char *fileExt = strrchr(fileName, '.');

    if (fileExt != NULL)
    {
        if (strcmp(fileExt, ext) == 0) result = true;
    }

    return result;
}