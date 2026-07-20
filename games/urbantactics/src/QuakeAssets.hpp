#pragma once

// Small application-level helpers for loading genuine Quake3 player-model
// assets (spacemarine40k: lower/upper/head .MD3 + .skin) — deliberately NOT
// engine code: the engine already has everything needed (load_md3_mesh,
// MorphMeshInstance, MorphTags) via its native MD3 pipeline, this just
// bridges the idTech3 .skin text format the engine loader doesn't parse
// itself. animation.cfg parsing used to live here too, but this model's
// legs-frame numbering didn't follow the standard idTech3 convention the
// file assumes, so the actual clip ranges are hardcoded directly in
// main.cpp instead (verified against each part's own real frame count).

#include <scene/Material.hpp>
#include <scene/AssetManager.hpp>
#include <scene/ByteArray.hpp>
#include <scene/Filesystem.hpp>
#include <scene/Math.hpp>
#include <cstdio>
#include <sstream>
#include <string>
#include <vector>

// Reads a text asset through fs::Filesystem — the SAME multi-root search
// (".", "..", "../..", "../../..", plus urbantactics' own extra levels)
// every other loader in this codebase already goes through. A plain
// std::ifstream(relativePath) only works when the process's cwd happens to
// already be the repo root; anywhere else it fails silently (ifstream
// doesn't throw) and the caller just gets an empty result with no error —
// exactly what made .skin parsing look "randomly broken" depending on how
// the binary was launched, even though every OTHER asset (textures,
// meshes, MD3s) loaded fine because THOSE already used this.
inline bool read_text_asset(const std::string& path, std::string& out)
{
    scene::ByteArray bytes;
    if (!fs::getFilesystem().readText(path.c_str(), bytes))
    {
        fprintf(stderr, "QuakeAssets: could not read '%s'\n", path.c_str());
        return false;
    }
    out.assign((const char*)bytes.data(), bytes.size());
    return true;
}

// .skin: "surfaceName,texturePath" per line (tag_* lines have an empty
// path and are skipped — they're attachment points, not real surfaces).
// Order preserved, matching MD3Loader's surface load order (file order).
inline std::vector<std::pair<std::string, std::string>> parse_skin(const std::string& path)
{
    std::vector<std::pair<std::string, std::string>> out;
    std::string text;
    if (!read_text_asset(path, text)) return out;
    std::istringstream f(text);
    std::string line;
    while (std::getline(f, line))
    {
        size_t comma = line.find(',');
        if (comma == std::string::npos) continue;
        std::string surf = line.substr(0, comma);
        std::string tex = line.substr(comma + 1);
        while (!tex.empty() && (tex.back() == '\r' || tex.back() == '\n')) tex.pop_back();
        if (tex.empty()) continue; // tag_* entries
        out.push_back({surf, tex});
    }
    return out;
}

// Overrides outMats' diffuse textures in .skin file order (skipping the
// tag_* pseudo-entries the .skin file also lists) — MD3Loader.cpp's own
// texture guess (surface/shader name + extension) doesn't find anything
// for models built around external .skin files like this one, so this
// replaces it. `texPath` is a Quake-style path like
// "models/players/spacemarine40k/default_1.tga"; only the basename is
// used, resolved against `assetDir` where the .tga files actually live.
inline void apply_skin_textures(std::vector<Material*>& outMats, const std::string& skinPath,
                                const std::string& assetDir, const std::string& namePrefix)
{
    auto entries = parse_skin(skinPath);
    assets::AssetManager& assets = assets::AssetManager::instance();
    size_t matIdx = 0;
    for (auto& entry : entries)
    {
        const std::string& texPath = entry.second;
        if (matIdx >= outMats.size()) break;
        size_t slash = texPath.find_last_of('/');
        std::string base = slash == std::string::npos ? texPath : texPath.substr(slash + 1);
        std::string full = assetDir + base;
        std::string cacheName = namePrefix + "_" + base;
        outMats[matIdx]->diffuse = assets.loadTexture(cacheName.c_str(), full.c_str());
        ++matIdx;
    }
}
