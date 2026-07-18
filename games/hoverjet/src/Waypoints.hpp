#pragma once

// Race track file I/O for the in-game waypoint editor (TAB free cam, N
// place, LEFT/RIGHT aim, BACKSPACE undo, DELETE clear all, F5 save — see
// main.cpp). Placement order IS gate order: a human clicking points in the
// world already knows the right order, no ordering heuristic needed.

#include <scene/ByteArray.hpp>
#include <scene/Filesystem.hpp>
#include <scene/Math.hpp>
#include <cstdio>
#include <sstream>
#include <string>
#include <vector>

namespace waypoints
{


inline bool save_track(const char* path, const std::vector<Vec3>& pos,
                       const std::vector<float>& yaw)
{
    static const char* kPrefixes[] = {"", "../", "../../", "../../../"};
    for (const char* prefix : kPrefixes)
    {
        std::string full = std::string(prefix) + path;
        FILE* f = fopen(full.c_str(), "w");
        if (!f) continue;
        for (size_t i = 0; i < pos.size(); ++i)
            fprintf(f, "%f %f %f %f\n", pos[i].x, pos[i].y, pos[i].z, yaw[i]);
        fclose(f);
        printf("waypoints: track saved to '%s'\n", full.c_str());
        return true;
    }
    return false;
}

inline bool load_track(const char* path, std::vector<Vec3>& pos, std::vector<float>& yaw)
{
    pos.clear();
    yaw.clear();
    scene::ByteArray bytes;
    if (!fs::getFilesystem().readFile(path, bytes)) return false;
    std::istringstream in(std::string((const char*)bytes.data(), bytes.size()));
    float x, y, z, ya;
    while (in >> x >> y >> z >> ya)
    {
        pos.push_back(Vec3(x, y, z));
        yaw.push_back(ya);
    }
    return pos.size() >= 3;
}

} // namespace waypoints
