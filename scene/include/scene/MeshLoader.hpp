#pragma once

#include "scene/Math.hpp"
#include <string>
#include <vector>

class Mesh;

// Reads the engine's binary mesh format, produced by the exporter tool
// (exporter/): assimp runs OFFLINE there — the engine only parses chunks,
// zero import libraries at runtime, so builds and load times stay fast.
//
// Format (little-endian): MESH magic + version, then chunks of
// [u32 id][u32 length][payload]:
//   MATS — material table (name, diffuse, specular, shininess, textures)
//   SKEL — skeleton (skipped here; skinned meshes come later)
//   BUFF — one surface: material index, flags, then nested VRTS
//          (pos/normal/uv) and IDXS (u32) chunks, SKIN when skinned
class MeshLoader
{
public:
    // What the file says about each material slot. The game maps the
    // texture names to real textures (AssetManager) and builds Materials.
    struct MaterialDesc
    {
        std::string name;
        Vec3 diffuse = Vec3(1.f, 1.f, 1.f);
        Vec3 specular = Vec3(0.f, 0.f, 0.f);
        float shininess = 32.f;
        std::vector<std::string> textures;
    };

    // Fills `out` with one surface per BUFF chunk (material_slot = the
    // file's material index), computes per-surface bounds and tangents.
    // Does NOT upload — call out.upload() with a live GL context.
    // Errors are reported through gl::Log.
    static bool load(const char* path, Mesh& out, std::vector<MaterialDesc>* materials = nullptr);
};
