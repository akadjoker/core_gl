// BSPLoader.cpp — Quake 3 BSP (IBSP v46) mesh loader for the coregl scene layer.
 // Imports polygon / mesh faces directly and tessellates Bezier patches.
// Vertices are converted from Z-up to Y-up.  One Material per texture group.
 

#include "scene/Mesh.hpp"
#include "scene/Material.hpp"
#include "scene/AssetManager.hpp"
#include "scene/BspInstance.hpp"
#include "scene/MeshInstance.hpp"
#include "scene/Math.hpp"
#include "scene/Filesystem.hpp"
#include "scene/IO.hpp"
#include "coregl/gl_log.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <string>
#include <unordered_map>
#include <vector>

namespace
{

// ---- Quake 3 BSP constants --------------------------------------------------
constexpr int kQ3Ident = 1347633737; // "IBSP"
constexpr int kQ3Version = 46;       // Quake 3
constexpr int kNumLumps = 17;
constexpr int kLumpEntities = 0;
constexpr int kLumpTextures = 1;
constexpr int kLumpPlanes = 2;
constexpr int kLumpNodes = 3;
constexpr int kLumpLeafs = 4;
constexpr int kLumpLeafBrushes = 6;
constexpr int kLumpModels = 7;
constexpr int kLumpVertices = 10;
constexpr int kLumpMeshVerts = 11;
constexpr int kLumpBrushes = 8;
constexpr int kLumpBrushSides = 9;
constexpr int kLumpFaces = 13;

// Set by clipBoxToBrush whenever it registers a hit — lets debug logging
// (BSP_DEBUG_STAIRS) report which CONTENTS_* flags actually blocked a trace
// (real Solid geometry vs. an invisible PlayerClip volume) without changing
// BspTraceResult's public shape for a diagnostic-only need.
int g_lastHitContents = 0;

constexpr int kFacePolygon = 1;
constexpr int kFacePatch = 2;
constexpr int kFaceMesh = 3;
constexpr int kLumpLightmaps = 14;

constexpr int kTextureSize = 72; // sizeof(bsp texture entry): name[64] + flags(4) + contents(4)
constexpr int kVertexSize = 44;  // sizeof(bsp vertex)
constexpr int kFaceSize = 104;   // sizeof(bsp face)
constexpr int kPlaneSize = 16;   // vec3 normal + float dist
constexpr int kNodeSize = 36;    // int planeIdx + int children[2] + int mins[3] + int maxs[3]
constexpr int kLeafSize = 48;    // 12 ints (cluster,area,mins[3],maxs[3],firstFace,numFaces,firstLeafBrush,numLeafBrushes) — real Q3 dleaf_t; confirmed against tmp/apocalyx/glbsp.h's BSPLeaf
constexpr int kBrushSize = 12;   // int firstSide + int numSides + int textureIdx
constexpr int kBrushSideSize = 8; // int planeIdx + int textureIdx
constexpr int kModelSize = 40;   // vec3 mins + vec3 maxs + int firstFace + int numFaces + int firstBrush + int numBrushes

// Brush CONTENTS_* handling lives in BspContents:: now (BspInstance.hpp) —
// worldCollision() takes a caller-chosen mask at trace time instead of this
// file baking one fixed "is this solid" bool in at load time. Including
// CONTENTS_PLAYERCLIP in the default mask was a real bug fix: on bigger,
// properly-compiled Quake3 maps (Urban Terror's) mappers lay invisible
// player-only clip brushes over jagged stairs/ramp geometry so the player
// slides on a smooth clip hull instead of the visible steps — missing that
// bit meant catching on the raw stair edges underneath, not a math bug in
// the trace itself.
constexpr float kContactEps = 0.03125f; // Quake's own DIST_EPSILON convention (1/32 unit)

constexpr float kEps = 1e-6f;
constexpr int kPatchTess = 5; // Bezier subdivision per 2x2 block

// ---- Binary helpers ---------------------------------------------------------
struct Lump
{
    int offset;
    int length;
};

struct BspVertex
{
    Vec3 position;
    Vec3 normal;
    Vec2 uv;
    Vec2 lmuv; // lightmap UV (vertex bytes 20..28)
};

struct Face
{
    int textureIdx;
    int faceType;
    int firstVert;
    int numVerts;
    int firstMeshVert;
    int numMeshVerts;
    int lm_index;    // lightmap page (-1 = none)
    int lm_start[2]; // pixel offset in lightmap page
    int lm_size[2];  // pixel size of lightmap region
    int patchW;
    int patchH;
};

int rdI32(const u8* data, int off)
{
    int v = 0;
    std::memcpy(&v, data + off, sizeof(int));
    return v;
}

float rdF32(const u8* data, int off)
{
    float v = 0.f;
    std::memcpy(&v, data + off, sizeof(float));
    return v;
}

std::string rdStr(const u8* data, u32 dataSize, int off)
{
    std::string s;
    s.reserve(32);
    while (off < (int)dataSize && data[off] != 0)
    {
        s.push_back(static_cast<char>(data[off]));
        ++off;
    }
    return s;
}

// Bounds-checked lump accessor.  Returns base byte pointer + element count.
const u8* lumpInfo(const u8* data, u32 dataSize, const Lump& l, int elemSize, int& count)
{
    if (l.offset < 0 || l.length < 0 || elemSize <= 0)
    {
        count = 0;
        return nullptr;
    }
    count = l.length / elemSize;
    if (l.offset + l.length > (int)dataSize)
    {
        count = 0;
        return nullptr;
    }
    return data + l.offset;
}

// Convert a BSP vertex into an engine MeshVertex (Y-up, normalised normal).
MeshVertex makeVertex(const BspVertex& bv)
{
    MeshVertex mv;
    mv.position = bv.position;
    Vec3 n = bv.normal;
    float lenSq = Vec3::Dot(n, n);
    mv.normal = (lenSq > kEps) ? n.normalized() : Vec3(0.f, 1.f, 0.f);
    mv.uv.x =1.f -bv.uv.x;
    mv.uv.y = bv.uv.y; // Quake3 UVs are upside-down in OpenGL
    mv.tangent = Vec4(1.f, 0.f, 0.f, 1.f);
    return mv;
}

// ---- Bezier patch helpers (quadratic, 3x3 control net) ----------------------
// Evaluates position, normal, and uv from 9 BspVertex control points.
static void patchBasis(float t, float& b0, float& b1, float& b2)
{
    float it = 1.f - t; b0 = it * it; b1 = 2.f * t * it; b2 = t * t;
}

static float evalScalar(float p00, float p10, float p20, float p01, float p11, float p21,
                        float p02, float p12, float p22,
                        float u0, float u1, float u2, float v0, float v1, float v2)
{
    float r0 = p00 * u0 + p10 * u1 + p20 * u2;
    float r1 = p01 * u0 + p11 * u1 + p21 * u2;
    float r2 = p02 * u0 + p12 * u1 + p22 * u2;
    return r0 * v0 + r1 * v1 + r2 * v2;
}

static BspVertex evalPatch(const BspVertex cp[9], float u, float v)
{
    float u0, u1, u2, v0, v1, v2;
    patchBasis(u, u0, u1, u2);
    patchBasis(v, v0, v1, v2);
    auto S = [&](float a, float b, float c, float d, float e, float f,
                 float g, float h, float i) {
        return evalScalar(a, b, c, d, e, f, g, h, i, u0, u1, u2, v0, v1, v2);
    };
    BspVertex o{};
    o.position.x = S(cp[0].position.x,cp[1].position.x,cp[2].position.x,
                     cp[3].position.x,cp[4].position.x,cp[5].position.x,
                     cp[6].position.x,cp[7].position.x,cp[8].position.x);
    o.position.y = S(cp[0].position.y,cp[1].position.y,cp[2].position.y,
                     cp[3].position.y,cp[4].position.y,cp[5].position.y,
                     cp[6].position.y,cp[7].position.y,cp[8].position.y);
    o.position.z = S(cp[0].position.z,cp[1].position.z,cp[2].position.z,
                     cp[3].position.z,cp[4].position.z,cp[5].position.z,
                     cp[6].position.z,cp[7].position.z,cp[8].position.z);
    o.normal.x   = S(cp[0].normal.x,cp[1].normal.x,cp[2].normal.x,
                     cp[3].normal.x,cp[4].normal.x,cp[5].normal.x,
                     cp[6].normal.x,cp[7].normal.x,cp[8].normal.x);
    o.normal.y   = S(cp[0].normal.y,cp[1].normal.y,cp[2].normal.y,
                     cp[3].normal.y,cp[4].normal.y,cp[5].normal.y,
                     cp[6].normal.y,cp[7].normal.y,cp[8].normal.y);
    o.normal.z   = S(cp[0].normal.z,cp[1].normal.z,cp[2].normal.z,
                     cp[3].normal.z,cp[4].normal.z,cp[5].normal.z,
                     cp[6].normal.z,cp[7].normal.z,cp[8].normal.z);
    o.uv.x       = S(cp[0].uv.x,cp[1].uv.x,cp[2].uv.x,
                     cp[3].uv.x,cp[4].uv.x,cp[5].uv.x,
                     cp[6].uv.x,cp[7].uv.x,cp[8].uv.x);
    o.uv.y       = S(cp[0].uv.y,cp[1].uv.y,cp[2].uv.y,
                     cp[3].uv.y,cp[4].uv.y,cp[5].uv.y,
                     cp[6].uv.y,cp[7].uv.y,cp[8].uv.y);
    o.lmuv.x     = S(cp[0].lmuv.x,cp[1].lmuv.x,cp[2].lmuv.x,
                     cp[3].lmuv.x,cp[4].lmuv.x,cp[5].lmuv.x,
                     cp[6].lmuv.x,cp[7].lmuv.x,cp[8].lmuv.x);
    o.lmuv.y     = S(cp[0].lmuv.y,cp[1].lmuv.y,cp[2].lmuv.y,
                     cp[3].lmuv.y,cp[4].lmuv.y,cp[5].lmuv.y,
                     cp[6].lmuv.y,cp[7].lmuv.y,cp[8].lmuv.y);
    return o;
}

// Per-shader info pulled out of scripts/*.shader — see shaderInfoMap().
struct ShaderInfo
{
    std::string image; // first real "map" stage, else qer_editorimage, else animFrames[0]
    bool sky = false;    // surfaceparm sky — rendered by SceneRenderer's own
                          // skybox/skydome/procedural pass, never as geometry
    bool fog = false;    // surfaceparm fog — a volumetric density marker
                          // (nonsolid, usually tcmod-scrolled), never a real
                          // textured wall/floor; we don't have a volumetric
                          // fog pass, so skip it rather than draw it flat
    bool trans = false;  // surfaceparm trans, or a non-opaque blendFunc stage
    bool additive = false; // blendFunc GL_ONE GL_ONE (flames/glows) vs alpha
    std::vector<std::string> animFrames; // "animMap <fps> f1 f2 ..." — first stage found
    float animFps = 0.f;
};

static std::string upper(const std::string& s)
{
    std::string r = s;
    for (char& c : r) c = (char)std::toupper((unsigned char)c);
    return r;
}

// Quake3 BSP texture names are often SHADER names (scripts/*.shader), not
// literal image files — e.g. "textures/gothic_trim/pitted_rust3_trans" has
// no such .tga on disk, but its shader script says:
//   textures/gothic_trim/pitted_rust3_trans
//   {
//       qer_editorimage textures/gothic_trim/pitted_rust3.tga
//       { map $lightmap ... }
//       { map textures/gothic_trim/pitted_rust3.tga ... }
//   }
// This scans every registered Filesystem folder's "scripts" subdirectory
// once (lazily, cached) and maps shaderName -> the first real image it
// finds (a stage's "map", skipping $lightmap/$whiteimage/other $specials;
// else the "qer_editorimage" line as a fallback) plus the sky/trans/blend
// flags callers need to decide how to draw the surface at all.
const std::unordered_map<std::string, ShaderInfo>& shaderInfoMap()
{
    static std::unordered_map<std::string, ShaderInfo> map;
    static bool built = false;
    if (built) return map;
    built = true;

    auto isWs = [](char c) { return std::isspace((unsigned char)c) != 0; };

    for (gl::u32 i = 0; i < fs::getFilesystem().pathCount(); ++i)
    {
        const fs::PathEntry* entry = fs::getFilesystem().getPath(i);
        if (!entry || entry->type != fs::PathEntry::FOLDER) continue;

        std::string scriptsDir = std::string(entry->path) + "/scripts";
        DIR* dir = opendir(scriptsDir.c_str());
        if (!dir) continue;

        struct dirent* de;
        while ((de = readdir(dir)) != nullptr)
        {
            std::string fname = de->d_name;
            if (fname.size() < 8 || fname.compare(fname.size() - 7, 7, ".shader") != 0) continue;

            // Read THIS folder's copy directly by its full path via the
            // engine's own cross-platform io::FileInterface — NOT
            // Filesystem::readText("scripts/" + fname), which re-searches
            // every registered folder by that logical name and always
            // returns whichever one is registered first. Several maps ship
            // same-named shader files (e.g. every map's own "sfx.shader"
            // alongside the shared quake/scripts/sfx.shader) with different
            // contents, so that always silently shadowed every folder
            // after the first — some shaders (flame1dark) only existed in
            // the copy that never got read.
            io::FileInterface* iface = io::getFileInterface();
            if (!iface) continue;
            std::string fullPath = scriptsDir + "/" + fname;
            io::FileHandle* handle = iface->open(fullPath.c_str());
            if (!handle) continue;
            gl::i64 sz = iface->size(handle);
            std::string text;
            if (sz > 0)
            {
                text.resize((size_t)sz);
                iface->seek(handle, 0, io::SeekMode::Begin);
                iface->read(handle, &text[0], (gl::u64)sz);
            }
            iface->close(handle);

            size_t pos = 0, len = text.size();
            while (pos < len)
            {
                while (pos < len && isWs(text[pos])) ++pos;
                if (pos + 1 < len && text[pos] == '/' && text[pos + 1] == '/')
                {
                    while (pos < len && text[pos] != '\n') ++pos;
                    continue;
                }
                if (pos >= len) break;
                if (text[pos] == '{' || text[pos] == '}') { ++pos; continue; }

                size_t start = pos;
                while (pos < len && !isWs(text[pos]) && text[pos] != '{') ++pos;
                std::string token = text.substr(start, pos - start);
                if (token.empty()) continue;

                // Real .shader files often put a comment banner between the
                // shader name and its opening '{' (e.g. sfx.shader's
                // q3dm14fog) — skip those too, not just whitespace, or the
                // '{' reads as a stray brace and the whole block silently
                // stops being associated with this shader name.
                size_t p2 = pos;
                while (true)
                {
                    while (p2 < len && isWs(text[p2])) ++p2;
                    if (p2 + 1 < len && text[p2] == '/' && text[p2 + 1] == '/')
                    {
                        while (p2 < len && text[p2] != '\n') ++p2;
                        continue;
                    }
                    break;
                }
                if (p2 >= len || text[p2] != '{') continue; // not a "name {" header
                pos = p2 + 1;

                const std::string& shaderName = token;
                std::string firstMap, editorImage;
                bool sky = false, fog = false, trans = false, additive = false;
                std::vector<std::string> animFrames;
                float animFps = 0.f;
                int depth = 1;
                int stageCount = 0;
                bool inFirstStage = false;
                while (pos < len && depth > 0)
                {
                    if (text[pos] == '{')
                    {
                        ++depth;
                        if (depth == 2) { ++stageCount; inFirstStage = (stageCount == 1); }
                        ++pos; continue;
                    }
                    if (text[pos] == '}')
                    {
                        if (depth == 2) inFirstStage = false;
                        --depth; ++pos; continue;
                    }
                    if (pos + 1 < len && text[pos] == '/' && text[pos + 1] == '/')
                    {
                        while (pos < len && text[pos] != '\n') ++pos;
                        continue;
                    }
                    if (isWs(text[pos])) { ++pos; continue; }

                    size_t ts = pos;
                    while (pos < len && !isWs(text[pos]) && text[pos] != '{' && text[pos] != '}') ++pos;
                    std::string kw = text.substr(ts, pos - ts);

                    if (kw == "qer_editorimage" || kw == "map")
                    {
                        while (pos < len && isWs(text[pos])) ++pos;
                        size_t vs = pos;
                        while (pos < len && !isWs(text[pos])) ++pos;
                        std::string val = text.substr(vs, pos - vs);
                        if (kw == "qer_editorimage") editorImage = val;
                        else if (firstMap.empty() && !val.empty() && val[0] != '$')
                            firstMap = val;
                    }
                    else if (kw == "surfaceparm")
                    {
                        while (pos < len && isWs(text[pos])) ++pos;
                        size_t vs = pos;
                        while (pos < len && !isWs(text[pos])) ++pos;
                        std::string val = upper(text.substr(vs, pos - vs));
                        if (val == "SKY") sky = true;
                        else if (val == "FOG") fog = true;
                        else if (val == "TRANS") trans = true;
                    }
                    else if (kw == "animMap")
                    {
                        // "animMap <fps> frame1.tga frame2.tga ..." — the
                        // torch/conduit/flame family (no static "map" stage
                        // at all, just a frame sequence). First animMap
                        // stage found wins, same "first one, not every one"
                        // convention as firstMap above — flame shaders
                        // layer TWO offset-by-one animMap stages for a
                        // flicker effect; one real cycling texture beats
                        // freezing on whichever qer_editorimage frame.
                        while (pos < len && isWs(text[pos]) && text[pos] != '\n') ++pos;
                        size_t vs = pos;
                        while (pos < len && !isWs(text[pos])) ++pos;
                        float fps = (float)std::atof(text.substr(vs, pos - vs).c_str());

                        std::vector<std::string> frames;
                        while (true)
                        {
                            while (pos < len && isWs(text[pos]) && text[pos] != '\n') ++pos;
                            if (pos >= len || text[pos] == '\n' || text[pos] == '{' || text[pos] == '}')
                                break;
                            size_t fs = pos;
                            while (pos < len && !isWs(text[pos])) ++pos;
                            frames.push_back(text.substr(fs, pos - fs));
                        }
                        if (animFrames.empty() && !frames.empty())
                        {
                            animFrames = frames;
                            animFps = fps;
                        }
                    }
                    else if (kw == "blendFunc")
                    {

                        while (pos < len && isWs(text[pos])) ++pos;
                        size_t vs = pos;
                        while (pos < len && !isWs(text[pos]) && text[pos] != '\n') ++pos;
                        std::string src = upper(text.substr(vs, pos - vs));
                        while (pos < len && isWs(text[pos]) && text[pos] != '\n') ++pos;
                        size_t vd = pos;
                        while (pos < len && !isWs(text[pos])) ++pos;
                        std::string dst = upper(text.substr(vd, pos - vd));

                        if (inFirstStage)
                        {
       
                            if (src == "ADD" || (src == "GL_ONE" && dst == "GL_ONE"))
                            {
                                trans = true; additive = true;
                            }
                            else if (src == "FILTER" ||
                                    (src == "GL_DST_COLOR" && dst == "GL_ZERO") ||
                                    (src == "GL_ONE" && dst == "GL_ZERO"))
                            {
                                // modulate / opaque replace — not trans
                            }
                            else
                            {
                                trans = true; // BLEND shorthand or any other GL_* pair
                            }
                        }
                    }
                }

                std::string chosen = !firstMap.empty() ? firstMap
                                     : !editorImage.empty() ? editorImage
                                     : (animFrames.empty() ? std::string() : animFrames[0]);
                ShaderInfo info;
                info.image = chosen;
                info.sky = sky;
                info.fog = fog;
                info.trans = trans;
                info.additive = additive;
                info.animFrames = animFrames;
                info.animFps = animFps;
                map[shaderName] = info;
            }
        }
        closedir(dir);
    }
    return map;
}

// Try loading a texture from `textureDir` + `name` with several extensions,
// then fall back to resolving `name` as a shader script name (see
// shaderInfoMap() above) before giving up to the checkerboard. Q3 BSP
// texture names are like "textures/base_floor/clanggrate"; we join directly
// with textureDir (the map root, e.g. "assets/bsp/oa_rpg3dm2/").
// AssetManager never returns null (checkerboard fallback).
gl::Texture* tryLoadTex(const std::string& textureDir, const std::string& name)
{
    const char* exts[] = {".tga", ".jpg", ".png", ".bmp"};

    for (const char* e : exts)
    {
        std::string fullPath = textureDir + name + e;
        if (fs::getFilesystem().exists(fullPath.c_str()))
            return assets::AssetManager::instance().loadTexture(name.c_str(), fullPath.c_str(), true);
    }

    const auto& shaders = shaderInfoMap();
    auto it = shaders.find(name);
    if (it != shaders.end())
    {
        const std::string& resolved = it->second.image;
        bool hasExt = resolved.size() > 4 && resolved[resolved.size() - 4] == '.';
        std::string base = hasExt ? resolved.substr(0, resolved.size() - 4) : resolved;

        // Try the shader's own extension first, but asset packs frequently
        // ship a different one than the original Quake3 .tga the shader
        // script names (e.g. this map's pitted_rust3.tga only exists as
        // pitted_rust3.jpg on disk) — fall through the same extension list
        // as the literal-name attempt above instead of taking the shader's
        // word for it.
        if (hasExt)
        {
            std::string fullPath = textureDir + resolved;
            if (fs::getFilesystem().exists(fullPath.c_str()))
                return assets::AssetManager::instance().loadTexture(name.c_str(), fullPath.c_str(), true);
        }
        for (const char* e : exts)
        {
            std::string fullPath = textureDir + base + e;
            if (fs::getFilesystem().exists(fullPath.c_str()))
                return assets::AssetManager::instance().loadTexture(name.c_str(), fullPath.c_str(), true);
        }
    }

    // Fallback — AssetManager returns checkerboard so the scene keeps rendering.
    return assets::AssetManager::instance().loadTexture(name.c_str(),
                                                        (textureDir + name + ".tga").c_str(),
                                                        true);
}

// ---- entities lump: "{ \"key\" \"value\" ... }" blocks, no factory yet ------
std::vector<BspInstance::Entity> parseEntities(const std::string& text)
{
    std::vector<BspInstance::Entity> ents;
    size_t i = 0;
    while (i < text.size())
    {
        size_t open = text.find('{', i);
        if (open == std::string::npos) break;
        size_t close = text.find('}', open);
        if (close == std::string::npos) break;

        BspInstance::Entity e;
        size_t p = open + 1;
        while (p < close)
        {
            size_t k0 = text.find('"', p);
            if (k0 == std::string::npos || k0 >= close) break;
            size_t k1 = text.find('"', k0 + 1);
            if (k1 == std::string::npos || k1 >= close) break;
            size_t v0 = text.find('"', k1 + 1);
            if (v0 == std::string::npos || v0 >= close) break;
            size_t v1 = text.find('"', v0 + 1);
            if (v1 == std::string::npos || v1 >= close) break;

            std::string key = text.substr(k0 + 1, k1 - k0 - 1);
            std::string value = text.substr(v0 + 1, v1 - v0 - 1);
            e.keyValues[key] = value;
            p = v1 + 1;
        }
        auto it = e.keyValues.find("classname");
        e.classname = (it != e.keyValues.end()) ? it->second : "";
     //  gl::Log::Info("[BSP] entity: classname='%s' keys=%zu", e.classname.c_str(), e.keyValues.size());
        ents.push_back(std::move(e));
        i = close + 1;
    }
    return ents;
}

// Q3 "origin" values are "x y z" in Z-up space; swap to engine Y-up, same
// convention as makeVertex()'s Z-up -> Y-up swap above.
bool parseOriginYUp(const std::string& s, Vec3& out)
{
    float x, y, z;
    if (std::sscanf(s.c_str(), "%f %f %f", &x, &y, &z) != 3) return false;
    out = Vec3(x, z, y);
    return true;
}

} // namespace

// ============================================================================
// BspInstance collision: recursive box trace through the BSP tree (Quake's
// classic CM_RecursiveHullCheck shape) + leaf/brush clip (ClipBoxToBrush).
// Own implementation against our own Vec3/types — not a port of any single
// reference; the technique is the standard one shared by apocalyx's GLBsp,
// 6dx's collider.cpp and Genesis3D's Trace.c (see the BSP plan for details).
// ============================================================================

bool BspInstance::clipBoxToBrush(const BspBrush& brush, const Vec3& origStart, const Vec3& origEnd,
                                 const Vec3& halfExtent, BspTraceResult& result) const
{
    // Always tested against the trace's ORIGINAL start/end — never a
    // node-split sub-segment — so `enterFrac` below is already in the
    // trace's own 0..1 scale, no remap needed. See the long comment on the
    // declaration in BspInstance.hpp for why that matters.
    float enterFrac = -1.f;
    float leaveFrac = 1.f;
    bool startsOut = false;
    bool getsOut = false;
    const BspPlane* clipPlane = nullptr;
    // Tracks the LEAST-penetrated side (largest d1, closest to 0) while the
    // box is fully embedded — the natural "push out this way" direction if
    // every side comes back negative. Without this, the startSolid branch
    // below had no plane to report and left result.normal at its default
    // (0,0,0): a degenerate zero-normal "hit" that slideVelocity's crease
    // resolver accepts for free (dot() with a zero vector is never negative)
    // and returns the ORIGINAL velocity unclipped — so the trace re-runs
    // next frame from the exact same embedded start, gets the same
    // zero-normal non-answer, forever. That's the "player just stops and
    // never moves again" symptom against doors/tight corners.
    float bestD1 = -1e30f;
    const BspPlane* bestPlane = nullptr;

    for (int i = 0; i < brush.numSides; ++i)
    {
        const BspBrushSide& side = m_bspBrushSides[brush.firstSide + i];
        const BspPlane& plane = m_bspPlanes[side.planeIndex];

        float offset = std::fabs(halfExtent.x * plane.normal.x) +
                       std::fabs(halfExtent.y * plane.normal.y) +
                       std::fabs(halfExtent.z * plane.normal.z);
        float dist = plane.dist + offset;

        float d1 = Vec3::Dot(plane.normal, origStart) - dist;
        float d2 = Vec3::Dot(plane.normal, origEnd) - dist;

        if (d1 > bestD1) { bestD1 = d1; bestPlane = &plane; }

        if (d2 > 0.f) getsOut = true;
        if (d1 > 0.f) startsOut = true;

        if (d1 > 0.f && d2 > 0.f) return false; // fully outside this plane, never enters the brush
        if (d1 <= 0.f && d2 <= 0.f) continue;   // fully behind this plane, doesn't clip the sweep

        // kContactEps shrinks the stop point slightly short of the true
        // geometric surface (entering) / slightly past it (leaving) — so a
        // resolved position is NEVER left exactly on a plane, where the
        // next frame's d1 would be an ambiguous ~0 and misclassify as
        // already-embedded. Same technique as apocalyx's GLBsp and
        // DigiBen's BSP Loader Part 6 (both use kEpsilon/DIST_EPSILON =
        // 0.03125f this exact way), not a bandaid on the result afterward.
        if (d1 > d2) // entering through this plane
        {
            float f = (d1 - kContactEps) / (d1 - d2);
            if (f > enterFrac) { enterFrac = f; clipPlane = &plane; }
        }
        else // leaving through this plane
        {
            float f = (d1 + kContactEps) / (d1 - d2);
            if (f < leaveFrac) leaveFrac = f;
        }
    }

    if (!startsOut)
    {
        // start point already inside the brush's solid volume
        result.startSolid = true;
        if (!getsOut && result.fraction > 0.f)
        {
            result.hit = true;
            result.fraction = 0.f;
            // Report the least-penetrated side as the blocking normal —
            // gives callers (slideVelocity's unstick step) a real direction
            // to push out along instead of a degenerate zero vector.
            result.normal = bestPlane ? bestPlane->normal : Vec3(0.f, 1.f, 0.f);
            g_lastHitContents = brush.contents;
        }
        return true;
    }

    if (enterFrac < leaveFrac && enterFrac > -1.f && enterFrac < result.fraction)
    {
        if (enterFrac < 0.f) enterFrac = 0.f;
        result.fraction = enterFrac;
        result.normal = clipPlane ? clipPlane->normal : Vec3(0.f, 1.f, 0.f);
        result.hit = true;
        g_lastHitContents = brush.contents;
        return true;
    }
    return false;
}

void BspInstance::traceLeaf(int leaf, const Vec3& origStart, const Vec3& origEnd,
                            const Vec3& halfExtent, int contentMask, BspTraceResult& result) const
{
    const BspLeaf& lf = m_bspLeafs[leaf];
    for (int i = 0; i < lf.numLeafBrushes; ++i)
    {
        int brushIdx = m_bspLeafBrushes[lf.firstLeafBrush + i];
        const BspBrush& brush = m_bspBrushes[brushIdx];
        if ((brush.contents & contentMask) == 0) continue;
        clipBoxToBrush(brush, origStart, origEnd, halfExtent, result);
    }
}

void BspInstance::traceNode(int node, float startFrac, float endFrac, const Vec3& start,
                            const Vec3& end, const Vec3& origStart, const Vec3& origEnd,
                            const Vec3& halfExtent, int contentMask, BspTraceResult& result) const
{
    if (result.fraction <= startFrac) return; // a closer hit was already found elsewhere

    if (node < 0)
    {
        traceLeaf(~node, origStart, origEnd, halfExtent, contentMask, result);
        return;
    }

    const BspNode& n = m_bspNodes[node];
    const BspPlane& plane = m_bspPlanes[n.planeIndex];

    float offset = std::fabs(halfExtent.x * plane.normal.x) +
                   std::fabs(halfExtent.y * plane.normal.y) +
                   std::fabs(halfExtent.z * plane.normal.z);

    float t1 = Vec3::Dot(plane.normal, start) - plane.dist;
    float t2 = Vec3::Dot(plane.normal, end) - plane.dist;

    if (t1 >= offset && t2 >= offset)
    {
        traceNode(n.children[0], startFrac, endFrac, start, end, origStart, origEnd, halfExtent,
                 contentMask, result);
        return;
    }
    if (t1 < -offset && t2 < -offset)
    {
        traceNode(n.children[1], startFrac, endFrac, start, end, origStart, origEnd, halfExtent,
                 contentMask, result);
        return;
    }

    // the sweep straddles this plane: split at the crossing point(s) and
    // recurse the near side first, then the far side. kContactEps here
    // matches clipBoxToBrush's — same reasoning, see its comment.
    int side;
    float frac1, frac2;
    if (t1 < t2)
    {
        float idist = 1.f / (t1 - t2);
        side = 1; // back first
        frac1 = (t1 - offset - kContactEps) * idist;
        frac2 = (t1 + offset + kContactEps) * idist;
    }
    else if (t1 > t2)
    {
        float idist = 1.f / (t1 - t2);
        side = 0; // front first
        frac1 = (t1 + offset + kContactEps) * idist;
        frac2 = (t1 - offset - kContactEps) * idist;
    }
    else
    {
        side = 0;
        frac1 = 0.f;
        frac2 = 1.f;
    }
    frac1 = frac1 < 0.f ? 0.f : (frac1 > 1.f ? 1.f : frac1);
    frac2 = frac2 < 0.f ? 0.f : (frac2 > 1.f ? 1.f : frac2);

    // near side: from `start` up to the first crossing point
    float midFrac1 = startFrac + (endFrac - startFrac) * frac1;
    Vec3 mid1 = start + (end - start) * frac1;
    traceNode(n.children[side], startFrac, midFrac1, start, mid1, origStart, origEnd, halfExtent,
             contentMask, result);

    // far side: from the second crossing point onward to `end` — NOT
    // `start` again (a real bug here: reusing `start` corrupted every
    // traversal that crossed more than one splitting plane, which a short
    // vertical drop rarely does but any long horizontal sweep always does —
    // exactly why floor collision looked fine while wall collision found
    // nothing at all).
    float midFrac2 = startFrac + (endFrac - startFrac) * frac2;
    Vec3 mid2 = start + (end - start) * frac2;
    traceNode(n.children[side ^ 1], midFrac2, endFrac, mid2, end, origStart, origEnd, halfExtent,
             contentMask, result);
}

BspTraceResult BspInstance::traceBox(const Vec3& start, const Vec3& end, const Vec3& halfExtent) const
{
    return worldCollision(start, end, halfExtent, BspContents::DefaultSolid);
}

// Modeled on Genesis3D's geWorld_Collision (see tmp/Genesis3D11/src/Game/
// GMain.c) — same shape (box + start/end + a content mask), general query
// traceBox is itself just a thin wrapper over. Doesn't identify which
// entity/model was hit yet (only worldspawn's static tree is walked here) —
// that needs per-entity brush lists, a separate future step for movers.
BspTraceResult BspInstance::worldCollision(const Vec3& start, const Vec3& end, const Vec3& halfExtent,
                                           int contentMask) const
{
    BspTraceResult result;
    result.fraction = 1.f;
    result.endPos = end;
    if (m_bspNodes.empty())
    {
        return result;
    }
    traceNode(m_bspRootNode, 0.f, 1.f, start, end, start, end, halfExtent, contentMask, result);
    result.endPos = start + (end - start) * result.fraction;
    return result;
}


static bool isWalkableGround(const Vec3& n)
{
    return n.y >= 0.7f;
}
Vec3 BspInstance::tryStepUp(const Vec3& start, const Vec3& end, const Vec3& halfExtent,
    bool* outStepped, bool* outGrounded) const
{
    if (outStepped) *outStepped = false;
    if (outGrounded) *outGrounded = false;


    constexpr float kStepHeight = 24.f;

    const Vec3 move = end - start;
    const Vec3 horizMove(move.x, 0.f, move.z);

    if (horizMove.length_squared() <= 1e-8f)
        return start;

    // Probe with a narrower horizontal footprint than the real body for the
    // up/forward/down search (full height kept — headroom still needs to be
    // real). A full-width box can straddle two risers at once on stairs
    // whose tread depth is shorter than the player's own width, wedging it
    // between steps instead of climbing (this was a real bug here — the
    // player got stuck partway up a narrower staircase, camera ending up
    // jammed into the geometry). Same technique most engines use: probe
    // narrow, then verify the full body actually fits at the result before
    // committing to it.
    const Vec3 probeExtent(halfExtent.x * 0.5f, halfExtent.y, halfExtent.z * 0.5f);

    // PASSO 1: Subir (Trace UP)
    // Em vez de teleportar, fazemos sweep para cima. Se batermos num teto baixo,
    // o traceUp.endPos para antes de atravessar geometria sólida.
    Vec3 upTarget = start + Vec3(0.f, kStepHeight, 0.f);
    BspTraceResult traceUp = traceBox(start, upTarget, probeExtent);

    Vec3 steppedStart = traceUp.endPos;

    // PASSO 2: Avançar (Trace FORWARD)
    // Tenta mover para a frente a partir do ponto mais alto que conseguimos subir.
    Vec3 forwardTarget = steppedStart + horizMove;
    BspTraceResult traceForward = traceBox(steppedStart, forwardTarget, probeExtent);

    // Crítico: Se o sweep horizontal mal avançou (bateu numa parede logo a seguir ao degrau),
    // consideramos o step um fracasso para evitar que o jogador fique preso em esquinas.
    if (traceForward.fraction < 0.01f)
        return start;

    Vec3 forwardEnd = traceForward.endPos;

    // PASSO 3: Descer (Trace DOWN)
    // Descemos a mesma altura que tentámos subir originalmente, mais uma margem (2.0f)
    // para garantir que a colisão com o chão regista corretamente.
    Vec3 downTarget = forwardEnd - Vec3(0.f, kStepHeight + 2.0f, 0.f);
    BspTraceResult traceDown = traceBox(forwardEnd, downTarget, probeExtent);

    // Se o sweep para baixo não bateu em nada, não é um degrau, é um buraco/precipício.
    if (!traceDown.hit)
        return start;

    // Se aterra num declive acentuado que não é "camiho", rejeita.
    if (!isWalkableGround(traceDown.normal))
        return start;

    // Verification: does the REAL, full-width body actually fit at the
    // narrow probe's result? A zero-length trace at that spot with the
    // real halfExtent just checks for embedding (startSolid) — if the
    // narrow probe found a landing too tight for the real body, reject
    // rather than wedge the player into geometry.
    BspTraceResult fitCheck = traceBox(traceDown.endPos, traceDown.endPos, halfExtent);
    if (fitCheck.startSolid)
        return start;

    // O Step foi bem sucedido.
    if (outStepped) *outStepped = true;
    if (outGrounded) *outGrounded = true;

    return traceDown.endPos;
}

// New, separate step-up — used ONLY by slideVelocity, tryStepUp above stays
// exactly as it was for moveAndSlide. Ported faithfully from Genesis3D's
// own MovePlayerUpStep (tmp/Genesis3D11/src/Game/GMain.c), not reinvented:
// the SAME full halfExtent for every trace (no narrowed probe box), a
// clean/fully-unobstructed rise required (any collision at all while going
// up aborts the step, not "however far it got"), a small FIXED forward
// nudge (0.5 units, opposite the blocking wall's normal) rather than the
// full remaining movement, then drop back onto the ground.
static Vec3 genesisTryStepUp(const BspInstance& world, const Vec3& start, const Vec3& wallNormal,
                             const Vec3& halfExtent, bool* outStepped, bool* outGrounded)
{
    if (outStepped) *outStepped = false;
    if (outGrounded) *outGrounded = false;

    // Set BSP_DEBUG_STAIRS=1 in the environment to log exactly which of the
    // three sub-traces below aborts a step attempt — cheaper than guessing
    // whether kStepHeight/the forward nudge is wrong for a given map.
    static const bool debug = true;

    constexpr float kStepHeight = 24.f;

    // Pop straight up.
    Vec3 pos1 = start;
    Vec3 pos2 = start + Vec3(0.f, kStepHeight, 0.f);
    BspTraceResult upTrace = world.traceBox(pos1, pos2, halfExtent);
    if (upTrace.hit)
    {
        if (debug) gl::Log::Warn("[stairs] up-trace blocked at frac=%.3f normal=(%.2f,%.2f,%.2f)",
                                  upTrace.fraction, upTrace.normal.x, upTrace.normal.y, upTrace.normal.z);
        return start; // anything at all in the way — this isn't a clean step
    }

    // Nudge forward, opposite the wall that blocked us.
    pos1 = pos2;
    pos2 = pos2 - wallNormal * 0.5f;
    BspTraceResult fwdTrace = world.traceBox(pos1, pos2, halfExtent);
    if (fwdTrace.hit)
    {
        if (debug) gl::Log::Warn("[stairs] forward-nudge blocked at frac=%.3f", fwdTrace.fraction);
        return start;
    }

    // Settle back onto the ground.
    pos1 = pos2;
    pos2.y -= (kStepHeight + 1.f);
    BspTraceResult downTrace = world.traceBox(pos1, pos2, halfExtent);
    if (!downTrace.hit)
    {
        if (debug) gl::Log::Warn("[stairs] down-trace found no floor within %.1f units", kStepHeight + 1.f);
        return start; // no floor within range — a ledge/drop, not a step
    }

    // Any ordinary vertical wall passes the two checks above trivially too —
    // going straight up in place rarely hits anything (walls don't overhang
    // the player's own footprint), and the tiny forward nudge just lands
    // back on the SAME floor the player was already standing on. Without a
    // minimum-rise floor, that reads as "stepped OK" at rise=0, the caller
    // nudges 0.5 units and skips the normal wall-slide entirely, and the
    // player crawls at 0.5 units/pass into a plain wall forever instead of
    // sliding along it — exactly the "walks into any wall and just stops"
    // symptom, not a stairs-specific one.
    constexpr float kMinStepRise = 1.f;
    if (downTrace.endPos.y - start.y < kMinStepRise)
    {
        if (debug) gl::Log::Warn("[stairs] rejected: rise=%.2f < min (%.1f) — this is a wall, not a step",
                                  downTrace.endPos.y - start.y, kMinStepRise);
        return start;
    }

    if (debug) gl::Log::Warn("[stairs] stepped OK, rise=%.2f", downTrace.endPos.y - start.y);
    if (outStepped) *outStepped = true;
    if (outGrounded) *outGrounded = isWalkableGround(downTrace.normal);
    return downTrace.endPos;
}

Vec3 BspInstance::moveAndSlide(const Vec3& start, const Vec3& end,
    const Vec3& halfExtent, BspCollisionResponse response, bool* outGrounded) const
{
    Vec3 pos = start;
    Vec3 remaining = end - start;
    bool grounded = false;

    for (int pass = 0; pass < 4; ++pass)
    {
        if (remaining.length_squared() <= 1e-8f)
            break;

        Vec3 target = pos + remaining;
        BspTraceResult tr = traceBox(pos, target, halfExtent);

        Vec3 reached = tr.endPos;
        Vec3 leftover = target - reached;
        pos = reached;

        if (!tr.hit)
            break;

        if (isWalkableGround(tr.normal))
            grounded = true;

        if (response == BspCollisionResponse::Stop)
            break;

        // Only try stepping against a near-VERTICAL surface — matches
        // Genesis3D's own player physics (tmp/Genesis3D11/src/Game/GMain.c,
        // CheckVelocity: `if (!Collision.Plane.Normal.Y)` — only when the
        // blocking plane's normal.Y is ~0, not merely "steeper than
        // walkable"). A moderately-steep ramp (say normal.y=0.4 — too steep
        // to stand on, but not a wall either) isn't a discrete step to pop
        // up onto; treating it as one made tryStepUp misfire on inclines,
        // producing exactly the "stuck on a small ramp" symptom. Anything
        // between "wall" and "walkable" now just slides/blocks like a
        // normal slope, same as everywhere else in this loop.
        const bool lateralWall = std::fabs(tr.normal.y) < 0.2f;
        const bool hasHorizontalIntent =
            (remaining.x * remaining.x + remaining.z * remaining.z) > 1e-8f;

        // the ordinary slide result for this pass — computed regardless of
        // whether we also try stepping, so the two can be compared
        float d = leftover.dot(tr.normal);
        Vec3 slid = leftover - tr.normal * d;
        if (response == BspCollisionResponse::SlideXZ)
            slid.y = 0.f;

        if (lateralWall && hasHorizontalIntent)
        {
            bool stepped = false;
            bool stepGrounded = false;
            Vec3 stepPos = tryStepUp(pos, target, halfExtent, &stepped, &stepGrounded);

            // Only take the step if it actually makes at least as much
            // horizontal progress as sliding along the wall would have —
            // this is what makes the LAST step of a staircase behave: once
            // you're on the top landing, sliding already goes straight
            // through (no riser left to climb), so it naturally wins over
            // stepping instead of tryStepUp needing to special-case "is
            // this the top." Same technique real engines use (Source's
            // StepMove / Quake3's PM_StepSlideMove) — compare, don't guess.
            if (stepped)
            {
                Vec3 stepHoriz(stepPos.x - pos.x, 0.f, stepPos.z - pos.z);
                Vec3 slideHoriz(slid.x, 0.f, slid.z);
                if (stepHoriz.length_squared() >= slideHoriz.length_squared() * 0.9f)
                {
                    pos = stepPos;
                    grounded = grounded || stepGrounded;

                    // Manter apenas a direção horizontal que faltava percorrer.
                    // Se não cortarmos o Y (0.f), o boneco afunda no chão.
                    Vec3 unMoved = target - pos;
                    remaining = Vec3(unMoved.x, 0.f, unMoved.z);
                    continue;
                }
            }
        }

        remaining = slid;
    }

    // Extra ground probe, independent of whatever happened during the move
    // above: a short trace straight down from the resolved position. Purely
    // relying on "was the last hit this pass a floor normal" misses cases
    // where a wide box is resting on a narrow stair tread — gravity's own
    // downward trace can land on a step's edge/corner or an already-settled
    // contact that never re-triggers a fresh hit this frame, so `grounded`
    // stays false even though the player is clearly standing on something
    // (symptom: can't jump while standing still on stairs). Same idea as a
    // CharacterController's separate "IsGrounded" foot-probe in Unity/Unreal
    // — decoupled from the movement trace entirely.
    if (!grounded)
    {
        BspTraceResult groundProbe = traceBox(pos, pos - Vec3(0.f, 4.f, 0.f), halfExtent);
        if (groundProbe.hit && isWalkableGround(groundProbe.normal))
            grounded = true;
    }

    if (outGrounded)
        *outGrounded = grounded;

    return pos;
}

// Genesis3D's CheckVelocity (tmp/Genesis3D11/src/Game/GMain.c), adapted to
// our types — own implementation, not a line-for-line port. Separate
// function from moveAndSlide on purpose; that one is untouched.
Vec3 BspInstance::slideVelocity(const Vec3& start, Vec3& velocity, const Vec3& halfExtent, float dt,
                                bool* outGrounded) const
{
    constexpr int kMaxHits = 4;
    constexpr int kMaxClipPlanes = 5;

    Vec3 pos = start;
    // Creases are always resolved against the ORIGINAL velocity this frame
    // started with, not whatever it's been clipped down to mid-loop — same
    // as Genesis3D projecting OriginalVelocity at every plane in its
    // Planes[] list, not the progressively-shrunk one.
    Vec3 primalVelocity = velocity;
    Vec3 planes[kMaxClipPlanes];
    int numPlanes = 0;
    bool grounded = false;
    // Genesis3D never shrinks TimeLeft by the collision ratio either (see
    // their own commented-out `//TimeLeft -= TimeLeft * Collision.Ratio;`)
    // — every pass re-tries with the FULL dt, just against a progressively
    // clipped velocity, so a single frame can still cover real distance
    // across up to kMaxHits bounces.
    const float timeLeft = dt;

    for (int hitCount = 0; hitCount < kMaxHits; ++hitCount)
    {
        if (velocity.length_squared() <= 1e-8f) break;

        Vec3 target = pos + velocity * timeLeft;
        BspTraceResult tr = worldCollision(pos, target, halfExtent, BspContents::DefaultSolid);

        if (!tr.hit)
        {
            pos = target;
            break; // covered the whole distance this pass wanted
        }

        // Embedded in solid (a door closing on the player, a tight corner):
        // fraction is stuck at 0 forever unless we physically push out along
        // the least-penetrated side first. Genesis3D's CheckPlayer does the
        // same "shove and re-try" rather than letting Velocity clip against
        // a degenerate contact.
        if (tr.startSolid)
        {
            pos = pos + tr.normal * 1.f;
            numPlanes = 0;
            continue;
        }

        if (isWalkableGround(tr.normal))
            grounded = true;

        // Real progress this pass means the OLD crease planes no longer
        // constrain anything — start the crease list fresh (Genesis3D:
        // `if (Collision.Ratio > 0.00f) { ...; NumPlanes = 0; }`). Only a
        // pass that made NO progress (already pinned exactly here) keeps
        // accumulating planes — that's the "stuck in a corner" case the
        // 2-plane crease resolution below exists for.
        if (tr.fraction > 1e-4f)
        {
            numPlanes = 0;
            velocity = primalVelocity;
        }
        pos = tr.endPos;

        // Genesis3D's own trigger: only when the blocking plane is exactly
        // vertical (`if (!Collision.Plane.Normal.Y)` in CheckVelocity) and
        // there's horizontal intent — then their MovePlayerUpStep port
        // (genesisTryStepUp above), NOT moveAndSlide's tryStepUp.
        const bool lateralWall = std::fabs(tr.normal.y) < 0.2f;
        const bool hasHorizontalIntent = (velocity.x * velocity.x + velocity.z * velocity.z) > 1e-8f;
        if (lateralWall && hasHorizontalIntent)
        {
            bool stepped = false;
            bool stepGrounded = false;
            Vec3 stepPos = genesisTryStepUp(*this, pos, tr.normal, halfExtent, &stepped, &stepGrounded);
            if (stepped)
            {
                pos = stepPos;
                grounded = grounded || stepGrounded;
                numPlanes = 0;
                continue; // re-trace next pass from the stepped position, same velocity
            }
        }

        if (numPlanes >= kMaxClipPlanes)
        {
            velocity = Vec3(0.f, 0.f, 0.f);
            break;
        }
        planes[numPlanes++] = tr.normal;

        // Look for a direction that clears EVERY plane hit so far this
        // stuck streak, not just the most recent one — project
        // primalVelocity onto each candidate plane in turn and check it
        // doesn't drive back INTO any of the others.
        Vec3 newVelocity(0.f, 0.f, 0.f);
        int i;
        for (i = 0; i < numPlanes; ++i)
        {
            float d = Vec3::Dot(primalVelocity, planes[i]);
            newVelocity = primalVelocity - planes[i] * d;

            int j;
            for (j = 0; j < numPlanes; ++j)
            {
                if (j == i) continue;
                if (Vec3::Dot(newVelocity, planes[j]) < 0.f) break; // still drives into another plane
            }
            if (j == numPlanes) break; // this one clears everything we've hit
        }

        if (i != numPlanes)
        {
            velocity = newVelocity;
        }
        else if (numPlanes == 2)
        {
            // Genuine crease: slide along the line where both planes
            // meet (their cross product) instead of the two single-plane
            // projections fighting each other every pass — this is the
            // piece moveAndSlide's naive single-plane slide doesn't have,
            // and why it can read as "stuck" in a corner or where a ramp
            // meets a wall.
            Vec3 dir = Vec3::Cross(planes[0], planes[1]);
            float len2 = dir.length_squared();
            if (len2 < 1e-10f)
            {
                velocity = Vec3(0.f, 0.f, 0.f);
                break;
            }
            dir = dir * (1.f / std::sqrt(len2));
            float d = Vec3::Dot(primalVelocity, dir);
            velocity = dir * d;
        }
        else
        {
            velocity = Vec3(0.f, 0.f, 0.f);
            break;
        }

        // Never let the corrected velocity point backward relative to what
        // was actually wanted this frame.
        if (Vec3::Dot(velocity, primalVelocity) <= 0.f)
        {
            velocity = Vec3(0.f, 0.f, 0.f);
            break;
        }
    }

    if (outGrounded)
        *outGrounded = grounded;

    return pos;
}

BspRayHit BspInstance::raycast(const Vec3& origin, const Vec3& direction, float maxDistance) const
{
    BspRayHit out;
    float len = direction.length();
    if (len < 1e-6f) return out;

    Vec3 dir = direction * (1.f / len);
    Vec3 end = origin + dir * maxDistance;
    BspTraceResult tr = traceBox(origin, end, Vec3(0.f, 0.f, 0.f));

    out.hit = tr.hit;
    out.point = tr.endPos;
    out.normal = tr.normal;
    out.distance = tr.fraction * maxDistance;
    return out;
}

int BspInstance::model_index_from_entity(const Entity& e) const
{
    auto it = e.keyValues.find("model");
    if (it == e.keyValues.end() || it->second.empty() || it->second[0] != '*') return -1;
    int idx = std::atoi(it->second.c_str() + 1);
    if (idx <= 0 || idx >= (int)m_bspModels.size()) return -1;
    return idx;
}

void BspInstance::traceModelBrushes(int modelIndex, const Vec3& localStart, const Vec3& localEnd,
                                    const Vec3& halfExtent, int contentMask, BspTraceResult& result) const
{
    if (modelIndex <= 0 || modelIndex >= (int)m_bspModels.size()) return;
    const BspModel& model = m_bspModels[modelIndex];
    for (int i = 0; i < model.numBrushes; ++i)
    {
        int brushIdx = model.firstBrush + i;
        if (brushIdx < 0 || brushIdx >= (int)m_bspBrushes.size()) continue;
        const BspBrush& brush = m_bspBrushes[brushIdx];
        if ((brush.contents & contentMask) == 0) continue;
        clipBoxToBrush(brush, localStart, localEnd, halfExtent, result);
    }
}

BspTraceResult BspInstance::traceModel(int modelIndex, const Vec3& modelOffset,
                                       const Vec3& start, const Vec3& end,
                                       const Vec3& halfExtent, int contentMask) const
{
    BspTraceResult result;
    result.fraction = 1.f;
    result.endPos = end;
    if (modelIndex <= 0 || modelIndex >= (int)m_bspModels.size())
        return result;
    traceModelBrushes(modelIndex, start - modelOffset, end - modelOffset, halfExtent, contentMask, result);
    result.endPos = start + (end - start) * result.fraction;
    return result;
}

BspTraceResult BspInstance::worldCollisionWithModels(const Vec3& start, const Vec3& end,
                                                    const Vec3& halfExtent, int contentMask,
                                                    const std::unordered_map<int, Vec3>& modelOffsets) const
{
    BspTraceResult result = worldCollision(start, end, halfExtent, contentMask);
    for (const auto& kv : modelOffsets)
    {
        BspTraceResult mr = traceModel(kv.first, kv.second, start, end, halfExtent, contentMask);
        if (mr.fraction < result.fraction)
        {
            result = mr;
        }
    }
    result.endPos = start + (end - start) * result.fraction;
    return result;
}

Vec3 BspInstance::slideVelocityWithModels(const Vec3& start, Vec3& velocity, const Vec3& halfExtent, float dt,
                                          const std::unordered_map<int, Vec3>& modelOffsets,
                                          bool* outGrounded) const
{
    constexpr int kMaxHits = 4;
    constexpr int kMaxClipPlanes = 5;

    Vec3 pos = start;
    Vec3 primalVelocity = velocity;
    Vec3 planes[kMaxClipPlanes];
    int numPlanes = 0;
    bool grounded = false;
    const float timeLeft = dt;

    for (int hitCount = 0; hitCount < kMaxHits; ++hitCount)
    {
        if (velocity.length_squared() <= 1e-8f) break;

        Vec3 target = pos + velocity * timeLeft;
        BspTraceResult tr = worldCollisionWithModels(pos, target, halfExtent, BspContents::DefaultSolid, modelOffsets);

        if (!tr.hit)
        {
            pos = target;
            break;
        }

        // See slideVelocity's identical check above: an embedded start
        // (e.g. a door model that just translated onto the player) must be
        // shoved out along a real plane first, or the loop re-finds the
        // same startSolid=0-fraction result forever and the player freezes.
        if (tr.startSolid)
        {
            pos = pos + tr.normal * 1.f;
            numPlanes = 0;
            continue;
        }

        if (isWalkableGround(tr.normal))
            grounded = true;

        if (tr.fraction > 1e-4f)
        {
            numPlanes = 0;
            velocity = primalVelocity;
        }
        pos = tr.endPos;

        const bool lateralWall = std::fabs(tr.normal.y) < 0.2f;
        const bool hasHorizontalIntent = (velocity.x * velocity.x + velocity.z * velocity.z) > 1e-8f;
        if (lateralWall && hasHorizontalIntent)
        {
            static const bool debugStairs =true;
            if (debugStairs)
                gl::Log::Warn("[stairs] hit at pos=(%.1f,%.1f,%.1f) normal=(%.2f,%.2f,%.2f) vel=(%.1f,%.1f,%.1f) contents=0x%x",
                              pos.x, pos.y, pos.z, tr.normal.x, tr.normal.y, tr.normal.z,
                              velocity.x, velocity.y, velocity.z, g_lastHitContents);

            bool stepped = false;
            bool stepGrounded = false;
            Vec3 stepPos = genesisTryStepUp(*this, pos, tr.normal, halfExtent, &stepped, &stepGrounded);
            if (stepped)
            {
                pos = stepPos;
                grounded = grounded || stepGrounded;
                numPlanes = 0;
                continue;
            }
        }

        if (numPlanes >= kMaxClipPlanes)
        {
            velocity = Vec3(0.f, 0.f, 0.f);
            break;
        }
        planes[numPlanes++] = tr.normal;

        Vec3 newVelocity(0.f, 0.f, 0.f);
        int i;
        for (i = 0; i < numPlanes; ++i)
        {
            float d = Vec3::Dot(primalVelocity, planes[i]);
            newVelocity = primalVelocity - planes[i] * d;

            int j;
            for (j = 0; j < numPlanes; ++j)
            {
                if (j == i) continue;
                if (Vec3::Dot(newVelocity, planes[j]) < 0.f) break;
            }
            if (j == numPlanes) break;
        }

        if (i != numPlanes)
        {
            velocity = newVelocity;
        }
        else if (numPlanes == 2)
        {
            Vec3 dir = Vec3::Cross(planes[0], planes[1]);
            float len2 = dir.length_squared();
            if (len2 < 1e-10f)
            {
                velocity = Vec3(0.f, 0.f, 0.f);
                break;
            }
            dir = dir * (1.f / std::sqrt(len2));
            float d = Vec3::Dot(primalVelocity, dir);
            velocity = dir * d;
        }
        else
        {
            velocity = Vec3(0.f, 0.f, 0.f);
            break;
        }

        if (Vec3::Dot(velocity, primalVelocity) <= 0.f)
        {
            velocity = Vec3(0.f, 0.f, 0.f);
            break;
        }
    }

    if (outGrounded)
        *outGrounded = grounded;

    return pos;
}

// Vec3 BspInstance::moveAndSlide(const Vec3& start, const Vec3& end, const Vec3& halfExtent,
//                                BspCollisionResponse response, bool* outGrounded) const
// {
//     Vec3 pos = start;
//     Vec3 remaining = end - start;
//     bool grounded = false;

//     for (int pass = 0; pass < 4; ++pass)
//     {
//         if (remaining.length_squared() < 1e-8f) break;

//         Vec3 target = pos + remaining;
//         BspTraceResult tr = traceBox(pos, target, halfExtent);
//         Vec3 reached = tr.endPos;
//         Vec3 leftover = target - reached;
//         pos = reached;

//         if (!tr.hit) break;
//         if (tr.normal.y > 0.7f) grounded = true;

//         if (response == BspCollisionResponse::Stop) break;

//         float d = Vec3::Dot(leftover, tr.normal);
//         Vec3 slid = leftover - tr.normal * d;
//         if (response == BspCollisionResponse::SlideXZ) slid.y = 0.f;
//         remaining = slid;
//     }

//     if (outGrounded) *outGrounded = grounded;
//     return pos;
// }

bool BspInstance::find_spawn_point(Vec3& outPos) const
{
    const Entity* fallback = nullptr;
    for (const Entity& e : m_entities)
    {
        if (e.classname == "info_player_start")
        {
            auto it = e.keyValues.find("origin");
            if (it != e.keyValues.end() && parseOriginYUp(it->second, outPos)) return true;
        }
        else if (e.classname == "info_player_deathmatch" && !fallback)
        {
            fallback = &e;
        }
    }
    if (fallback)
    {
        auto it = fallback->keyValues.find("origin");
        if (it != fallback->keyValues.end() && parseOriginYUp(it->second, outPos)) return true;
    }
    return false;
}

bool BspInstance::entity_origin(const Entity& e, Vec3& outPos)
{
    auto it = e.keyValues.find("origin");
    if (it == e.keyValues.end()) return false;
    return parseOriginYUp(it->second, outPos);
}

bool BspInstance::entity_model_bounds(const Entity& e, Vec3& outMin, Vec3& outMax) const
{
    auto it = e.keyValues.find("model");
    if (it == e.keyValues.end() || it->second.empty() || it->second[0] != '*') return false;

    int idx = std::atoi(it->second.c_str() + 1);
    if (idx <= 0 || idx >= (int)m_bspModels.size()) return false; // model 0 is worldspawn, never referenced

    const BspModel& m = m_bspModels[idx];
    outMin = m.mins;
    outMax = m.maxs;
    return true;
}

int BspInstance::remove_entities(const std::string& classname)
{
    size_t before = m_entities.size();
    m_entities.erase(std::remove_if(m_entities.begin(), m_entities.end(),
                                    [&](const Entity& e) { return e.classname == classname; }),
                     m_entities.end());
    return static_cast<int>(before - m_entities.size());
}

int BspInstance::remove_entities_except(const std::string& keepClassname)
{
    size_t before = m_entities.size();
    m_entities.erase(std::remove_if(m_entities.begin(), m_entities.end(),
                                    [&](const Entity& e) { return e.classname != keepClassname; }),
                     m_entities.end());
    return static_cast<int>(before - m_entities.size());
}

bool assets::AssetManager::load_bsp_collision(BspInstance* inst, const char* path)
{
    if (!inst || !path) return false;

    scene::ByteArray bytes;
    if (!fs::getFilesystem().readFile(path, bytes))
    {
        gl::Log::Error("[BSP] cannot read '%s'", path);
        return false;
    }
    const u8* data = bytes.data();
    const u32 dataSize = bytes.size();

    if (dataSize < 8 + kNumLumps * 8)
    {
        gl::Log::Error("[BSP] truncated header '%s'", path);
        return false;
    }
    if (rdI32(data, 0) != kQ3Ident || rdI32(data, 4) != kQ3Version)
    {
        gl::Log::Error("[BSP] bad magic / version in '%s'", path);
        return false;
    }

    inst->loadCollisionFromBytes(data, dataSize, path);
    return true;
}

void BspInstance::loadCollisionFromBytes(const u8* data, u32 dataSize, const char* path)
{
    BspInstance* inst = this; // keep the body below identical to before the refactor

    std::vector<Lump> lumps(kNumLumps);
    for (int i = 0; i < kNumLumps; ++i)
    {
        lumps[i].offset = rdI32(data, 8 + i * 8);
        lumps[i].length = rdI32(data, 8 + i * 8 + 4);
    }

    // ---- textures: need "contents" (offset 68) to know which brushes are solid
    int numTextures = 0;
    const u8* texBase = lumpInfo(data, dataSize, lumps[kLumpTextures], kTextureSize, numTextures);
    std::vector<int> textureContents;
    textureContents.reserve(numTextures);
    for (int i = 0; i < numTextures; ++i)
        textureContents.push_back(rdI32(data, static_cast<int>((texBase - data) + i * kTextureSize + 68)));

    // ---- planes ---------------------------------------------------------
    int numPlanes = 0;
    const u8* planeBase = lumpInfo(data, dataSize, lumps[kLumpPlanes], kPlaneSize, numPlanes);
    inst->m_bspPlanes.clear();
    inst->m_bspPlanes.reserve(numPlanes);
    for (int i = 0; i < numPlanes; ++i)
    {
        int o = static_cast<int>((planeBase - data) + i * kPlaneSize);
        // Z-up -> Y-up, same swap as vertices: (x, z, y).
        BspInstance::BspPlane p;
        p.normal = Vec3(rdF32(data, o), rdF32(data, o + 8), rdF32(data, o + 4));
        p.dist = rdF32(data, o + 12);
        inst->m_bspPlanes.push_back(p);
    }

    // ---- nodes ------------------------------------------------------------
    int numNodes = 0;
    const u8* nodeBase = lumpInfo(data, dataSize, lumps[kLumpNodes], kNodeSize, numNodes);
    inst->m_bspNodes.clear();
    inst->m_bspNodes.reserve(numNodes);
    for (int i = 0; i < numNodes; ++i)
    {
        int o = static_cast<int>((nodeBase - data) + i * kNodeSize);
        BspInstance::BspNode n;
        n.planeIndex = rdI32(data, o);
        n.children[0] = rdI32(data, o + 4);
        n.children[1] = rdI32(data, o + 8);
        inst->m_bspNodes.push_back(n);
    }
    inst->m_bspRootNode = 0; // Q3's world tree always roots at node 0

    // ---- leafs --------------------------------------------------------------
    int numLeafs = 0;
    const u8* leafBase = lumpInfo(data, dataSize, lumps[kLumpLeafs], kLeafSize, numLeafs);
    inst->m_bspLeafs.clear();
    inst->m_bspLeafs.reserve(numLeafs);
    for (int i = 0; i < numLeafs; ++i)
    {
        int o = static_cast<int>((leafBase - data) + i * kLeafSize);
        BspInstance::BspLeaf lf;
        // 12 ints: cluster,area,mins[3],maxs[3],firstface,numfaces,firstbrush,numbrushes
        lf.firstLeafBrush = rdI32(data, o + 40);
        lf.numLeafBrushes = rdI32(data, o + 44);
        inst->m_bspLeafs.push_back(lf);
    }

    // ---- leafbrushes --------------------------------------------------------
    int numLeafBrushes = 0;
    const u8* lbBase = lumpInfo(data, dataSize, lumps[kLumpLeafBrushes], 4, numLeafBrushes);
    inst->m_bspLeafBrushes.clear();
    inst->m_bspLeafBrushes.reserve(numLeafBrushes);
    for (int i = 0; i < numLeafBrushes; ++i)
        inst->m_bspLeafBrushes.push_back(rdI32(data, static_cast<int>((lbBase - data) + i * 4)));

    // ---- brushsides -----------------------------------------------------
    int numBrushSides = 0;
    const u8* bsBase = lumpInfo(data, dataSize, lumps[kLumpBrushSides], kBrushSideSize, numBrushSides);
    inst->m_bspBrushSides.clear();
    inst->m_bspBrushSides.reserve(numBrushSides);
    for (int i = 0; i < numBrushSides; ++i)
    {
        int o = static_cast<int>((bsBase - data) + i * kBrushSideSize);
        BspInstance::BspBrushSide s;
        s.planeIndex = rdI32(data, o);
        inst->m_bspBrushSides.push_back(s);
    }

    // ---- brushes --------------------------------------------------------
    int numBrushes = 0;
    const u8* brushBase = lumpInfo(data, dataSize, lumps[kLumpBrushes], kBrushSize, numBrushes);
    inst->m_bspBrushes.clear();
    inst->m_bspBrushes.reserve(numBrushes);
    for (int i = 0; i < numBrushes; ++i)
    {
        int o = static_cast<int>((brushBase - data) + i * kBrushSize);
        BspInstance::BspBrush b;
        b.firstSide = rdI32(data, o);
        b.numSides = rdI32(data, o + 4);
        int texIdx = rdI32(data, o + 8);
        b.contents = (texIdx >= 0 && texIdx < (int)textureContents.size()) ? textureContents[texIdx] : 0;
        inst->m_bspBrushes.push_back(b);
    }

    // ---- models (brush entities' own geometry — model 0 is worldspawn) ---
    int numModels = 0;
    const u8* modelBase = lumpInfo(data, dataSize, lumps[kLumpModels], kModelSize, numModels);
    inst->m_bspModels.clear();
    inst->m_bspModels.reserve(numModels);
    for (int i = 0; i < numModels; ++i)
    {
        int o = static_cast<int>((modelBase - data) + i * kModelSize);
        BspInstance::BspModel md;
        // Z-up -> Y-up, same (x, z, y) swap as everything else in this file.
        md.mins = Vec3(rdF32(data, o), rdF32(data, o + 8), rdF32(data, o + 4));
        md.maxs = Vec3(rdF32(data, o + 12), rdF32(data, o + 20), rdF32(data, o + 16));
        md.firstFace = rdI32(data, o + 24);
        md.numFaces = rdI32(data, o + 28);
        md.firstBrush = rdI32(data, o + 32);
        md.numBrushes = rdI32(data, o + 36);
        inst->m_bspModels.push_back(md);
    }

    // ---- entities (raw text) ---------------------------------------------
    const Lump& entLump = lumps[kLumpEntities];
    std::string entText;
    if (entLump.offset >= 0 && entLump.length > 0 &&
        entLump.offset + entLump.length <= (int)dataSize)
    {
        entText.assign(reinterpret_cast<const char*>(data + entLump.offset), entLump.length);
    }
    inst->m_entities = parseEntities(entText);

    
    std::unordered_map<std::string, int> classCounts;
    for (const BspInstance::Entity& e : inst->m_entities) ++classCounts[e.classname];
    std::string breakdown;
    for (const auto& kv : classCounts)
    {
        if (!breakdown.empty()) breakdown += ", ";
        breakdown += kv.first + " x" + std::to_string(kv.second);
    }

    gl::Log::Info("[BSP] '%s' collision: planes=%d nodes=%d leafs=%d brushes=%d models=%d entities=%d",
                  path, numPlanes, numNodes, numLeafs, numBrushes, numModels,
                  (int)inst->m_entities.size());
    gl::Log::Info("[BSP] '%s' entity classes: %s", path, breakdown.c_str());
}

// Build one mesh from the faces that belong to a specific BSP model.  The
// caller already knows which model index each face maps to (faceToModel).
// Materials are created fresh and handed to the mesh (set_owned_materials).
static Mesh* buildBspModelMesh(const char* meshName,
                               const std::vector<Face>& faces,
                               const std::vector<int>& faceToModel,
                               int modelIndex,
                               const std::vector<BspVertex>& verts,
                               const std::vector<int>& meshVerts,
                               const std::vector<std::string>& textures,
                               const std::vector<gl::Texture*>& lightmapTextures,
                               const std::string& texDir,
                               std::vector<Material*>* out_mats = nullptr)
{
    std::vector<Material*> localMats;

    // Group faces belonging to this model by texture index.
    std::unordered_map<int, std::vector<int>> group;
    std::vector<int> order;
    for (int i = 0; i < (int)faces.size(); ++i)
    {
        if (faceToModel[i] != modelIndex) continue;
        int t = faces[i].textureIdx;
        if (group.find(t) == group.end()) order.push_back(t);
        group[t].push_back(i);
    }

    if (order.empty()) return nullptr;

    Mesh* m = assets::AssetManager::instance().createMesh(meshName);
    if (!m) return nullptr;

    std::vector<MeshVertex> outVerts;
    std::vector<Vec2> outLmUvs;
    std::vector<u32> outIndices;
    outVerts.reserve(verts.size());
    outLmUvs.reserve(verts.size());
    outIndices.reserve(meshVerts.size());

    struct SurfDef { u32 start, count; int slot; BoundingBox bb; };
    std::vector<SurfDef> surfDefs;

    int numVerts = (int)verts.size();
    int surfIdx = 0;
    for (int ti : order)
    {
        const std::vector<int>& faceList = group[ti];

        const u32 startIndex = static_cast<u32>(outIndices.size());

        for (int fi : faceList)
        {
            const Face& f = faces[fi];

            if (f.faceType == kFacePolygon || f.faceType == kFaceMesh)
            {
                for (int t = 0; t < f.numMeshVerts; ++t)
                {
                    int mvi = f.firstMeshVert + t;
                    if (mvi < 0 || mvi >= (int)meshVerts.size()) continue;
                    int vIdx = f.firstVert + meshVerts[mvi];
                    if (vIdx >= 0 && vIdx < numVerts)
                    {
                        outIndices.push_back(static_cast<u32>(outVerts.size()));
                        outVerts.push_back(makeVertex(verts[vIdx]));
                        outLmUvs.push_back(verts[vIdx].lmuv);
                    }
                }
            }
            else if (f.faceType == kFacePatch)
            {
                for (int by = 0; by + 2 < f.patchH; by += 2)
                {
                    for (int bx = 0; bx + 2 < f.patchW; bx += 2)
                    {
                        BspVertex cp[9];
                        bool bad = false;
                        for (int py = 0; py < 3; ++py)
                        {
                            for (int px = 0; px < 3; ++px)
                            {
                                int vi = f.firstVert + (by + py) * f.patchW + (bx + px);
                                if (vi < 0 || vi >= numVerts) { bad = true; break; }
                                cp[py * 3 + px] = verts[vi];
                            }
                            if (bad) break;
                        }
                        if (bad) continue;

                        const int n = kPatchTess + 1;
                        u32 baseVert = static_cast<u32>(outVerts.size());
                        for (int yy = 0; yy < n; ++yy)
                            for (int xx = 0; xx < n; ++xx)
                            {
                                float s = static_cast<float>(xx) / kPatchTess;
                                float t = static_cast<float>(yy) / kPatchTess;
                                BspVertex ev = evalPatch(cp, s, t);
                                outVerts.push_back(makeVertex(ev));
                                outLmUvs.push_back(ev.lmuv);
                            }

                        for (int yy = 0; yy < kPatchTess; ++yy)
                            for (int xx = 0; xx < kPatchTess; ++xx)
                            {
                                u32 i0 = baseVert + (yy * n + xx);
                                u32 i1 = i0 + 1;
                                u32 i2 = i0 + n;
                                u32 i3 = i2 + 1;
                                outIndices.push_back(i0);
                                outIndices.push_back(i2);
                                outIndices.push_back(i1);
                                outIndices.push_back(i1);
                                outIndices.push_back(i2);
                                outIndices.push_back(i3);
                            }
                    }
                }
            }
        }

        const u32 indexCount = static_cast<u32>(outIndices.size()) - startIndex;
        if (indexCount == 0) continue;

        Vec3 mn(MaxFloat, MaxFloat, MaxFloat);
        Vec3 mx(-MaxFloat, -MaxFloat, -MaxFloat);
        for (u32 i = startIndex; i < (u32)outIndices.size(); ++i)
        {
            const Vec3& p = outVerts[outIndices[i]].position;
            mn = mn.Min(p);
            mx = mx.Max(p);
        }

        std::string texName = (ti >= 0 && ti < (int)textures.size() && !textures[ti].empty())
                                  ? textures[ti]
                                  : ("surf_" + std::to_string(surfIdx));
        Material* mat = new Material();
        gl::Texture* tex = tryLoadTex(texDir, texName);
        mat->diffuse = tex;
        mat->double_sided = false;
        mat->lightmapped = true;
        mat->detail_scale = 1.0f;
        // Only matters for per-model meshes (func_door, ...): those end up
        // on a plain MeshInstance drawn by the forward/dynamic-lighting
        // shader, which isn't set up for this map (no matching ambient/
        // shadow tuning) and rendered them solid black. The worldspawn
        // mesh (model 0) ignores this — it's drawn through SceneRenderer's
        // own m_bsp shader, which never reads mat->unlit at all.
        mat->unlit = true;
        {
            auto sit = shaderInfoMap().find(texName);
            static const bool debugTex = std::getenv("BSP_DEBUG_TEX") != nullptr;
            if (debugTex)
            {
                Vec3 c = (mn + mx) * 0.5f;
                gl::Log::Warn("[tex] '%s' center=(%.0f,%.0f,%.0f) shader=%s sky=%d trans=%d",
                              texName.c_str(), c.x, c.y, c.z,
                              (sit != shaderInfoMap().end()) ? sit->second.image.c_str() : "(none)",
                              (sit != shaderInfoMap().end()) ? (int)sit->second.sky : 0,
                              (sit != shaderInfoMap().end()) ? (int)sit->second.trans : 0);
            }
            if (sit != shaderInfoMap().end())
            {
                if (sit->second.trans)
                {
                    mat->blend = true;
                    mat->additive = sit->second.additive;
                    mat->double_sided = true; // glass/flames/fences: seen from both sides
                }
                if (!sit->second.animFrames.empty() && sit->second.animFps > 0.f)
                {
                    for (const std::string& frame : sit->second.animFrames)
                    {
                        bool hasExt = frame.size() > 4 && frame[frame.size() - 4] == '.';
                        std::string base = hasExt ? frame.substr(0, frame.size() - 4) : frame;
                        mat->anim_frames.push_back(tryLoadTex(texDir, base));
                    }
                    mat->anim_fps = sit->second.animFps;
                    mat->diffuse = mat->anim_frames[0];
                }
            }
        }
        if (!faceList.empty())
        {
            int lmIdx = faces[faceList[0]].lm_index;
            if (lmIdx >= 0 && lmIdx < (int)lightmapTextures.size())
                mat->detail = lightmapTextures[lmIdx];
        }
        if (out_mats)
            out_mats->push_back(mat);
        else
            localMats.push_back(mat);

        surfDefs.push_back({startIndex, indexCount, surfIdx, BoundingBox(mn, mx)});
        ++surfIdx;
    }

    if (outVerts.empty() || outIndices.empty())
    {
        delete m; // createMesh already registered it; this is a leak-risk, but empty model meshes are rare
        return nullptr;
    }

    m->set_data(outVerts.data(), static_cast<u32>(outVerts.size()), outIndices.data(),
                static_cast<u32>(outIndices.size()));
    for (const SurfDef& s : surfDefs)
        m->add_surface(s.start, s.count, s.slot, s.bb);
    m->set_lightmap_uvs(outLmUvs.data(), static_cast<u32>(outLmUvs.size()));
    if (out_mats)
    {
        for (Material* mat : localMats)
            out_mats->push_back(mat);
    }
    m->set_owned_materials(localMats);
    // The worldspawn mesh (model 0) is drawn through SceneRenderer's own
    // m_bsp shader, which never reads tangents — but per-model meshes
    // (func_door, ...) end up on a plain MeshInstance instead, drawn
    // through the regular forward shader, which DOES need a real tangent
    // basis (location 2). Without this they still uploaded fine (the
    // attribute just read zeroed data), but a degenerate/zero tangent
    // basis in the forward shader's lighting math renders solid black.
    m->compute_tangents();
    m->upload();
    return m;
}

Mesh* assets::AssetManager::load_bsp_mesh(const char* name, const char* path,
                     std::vector<Material*>& out_mats,
                     const char* textureDir,
                     BspInstance* collision,
                     std::unordered_map<int, Mesh*>* out_modelMeshes)
{
    if (!name || !path) return nullptr;

    // Already loaded? (rare path: a second BspInstance for the same map —
    // still needs its own collision fill, so this falls back to a real
    // re-read rather than skip it silently)
    Mesh* existing = getMesh(name);
    if (existing)
    {
        if (collision) load_bsp_collision(collision, path);
        return existing;
    }

    // ---- read the whole file into memory ------------------------------------
    scene::ByteArray bytes;
    if (!fs::getFilesystem().readFile(path, bytes))
    {
        gl::Log::Error("[BSP] cannot read '%s'", path);
        return nullptr;
    }
    const u8* data = bytes.data();
    const u32 dataSize = bytes.size();

    if (dataSize < 8 + kNumLumps * 8)
    {
        gl::Log::Error("[BSP] truncated header '%s'", path);
        return nullptr;
    }
    if (rdI32(data, 0) != kQ3Ident || rdI32(data, 4) != kQ3Version)
    {
        gl::Log::Error("[BSP] bad magic / version in '%s'", path);
        return nullptr;
    }

    // Collision + entities, straight off this same in-memory buffer — no
    // second file read (see BspInstance::loadCollisionFromBytes's comment).
    if (collision) collision->loadCollisionFromBytes(data, dataSize, path);

    // ---- parse lump table ---------------------------------------------------
    std::vector<Lump> lumps(kNumLumps);
    for (int i = 0; i < kNumLumps; ++i)
    {
        lumps[i].offset = rdI32(data, 8 + i * 8);
        lumps[i].length = rdI32(data, 8 + i * 8 + 4);
    }

    // ---- parse textures -----------------------------------------------------
    int numTextures = 0;
    const u8* texBase = lumpInfo(data, dataSize, lumps[kLumpTextures], kTextureSize, numTextures);
    std::vector<std::string> textures;
    textures.reserve(numTextures);
    for (int i = 0; i < numTextures; ++i)
    {
        textures.push_back(rdStr(data, dataSize, (texBase - data) + i * kTextureSize));
    }

    // ---- parse lightmaps (lump 14: 128x128 RGB pages) -----------------------
    // Q3 lightmap pages are stored as raw, ungamma-corrected radiosity
    // samples — applied straight to the framebuffer they read flat/dark, so
    // every Q3-derived renderer re-brightens them on load (the classic
    // r_lightmapgamma/overbright-bits knob). kLightmapGamma < 1 brightens
    // midtones (out = (in/255)^gamma * 255); 1.0 would be a no-op.
    constexpr float kLightmapGamma = 0.3f;
    constexpr int kLmSize = 128 * 128 * 3;
    int numLightmaps = 0;
    const u8* lmBase = lumpInfo(data, dataSize, lumps[kLumpLightmaps], kLmSize, numLightmaps);
    std::vector<gl::Texture*> lightmapTextures;
    lightmapTextures.reserve(numLightmaps);
    u8 gammaLUT[256];
    for (int v = 0; v < 256; ++v)
    {
        float f = std::pow(static_cast<float>(v) / 255.f, kLightmapGamma) * 255.f;
        gammaLUT[v] = static_cast<u8>(f < 0.f ? 0.f : (f > 255.f ? 255.f : f));
    }
    for (int i = 0; i < numLightmaps; ++i)
    {
        const u8* src = lmBase + i * kLmSize;
        std::vector<u8> rgba(128 * 128 * 4);
        for (int p = 0; p < 128 * 128; ++p)
        {
            rgba[p * 4 + 0] = gammaLUT[src[p * 3 + 0]];
            rgba[p * 4 + 1] = gammaLUT[src[p * 3 + 1]];
            rgba[p * 4 + 2] = gammaLUT[src[p * 3 + 2]];
            rgba[p * 4 + 3] = 255;
        }
        std::string lmName = std::string(name) + "/lm_" + std::to_string(i);
        gl::Texture* lmTex = assets::AssetManager::instance().createTexture(lmName.c_str());
        if (lmTex)
        {
            lmTex->Load2D(rgba.data(), 128, 128, gl::TextureFormat::RGBA8);
            lmTex->SetWrap(gl::TextureWrap::CLAMP_TO_EDGE, gl::TextureWrap::CLAMP_TO_EDGE);
            lmTex->SetFilter(gl::TextureFilter::LINEAR, gl::TextureFilter::LINEAR);
            lightmapTextures.push_back(lmTex);
        }
    }
    gl::Log::Info("[BSP] %d lightmap pages", numLightmaps);

    // ---- parse vertices (Z-up -> Y-up swap) ---------------------------------
    int numVerts = 0;
    const u8* vertBase = lumpInfo(data, dataSize, lumps[kLumpVertices], kVertexSize, numVerts);
    std::vector<BspVertex> verts;
    verts.reserve(numVerts);
    for (int i = 0; i < numVerts; ++i)
    {
        int o = static_cast<int>((vertBase - data) + i * kVertexSize);
        BspVertex v;
        // Q3 is Z-up; swap Y and Z.  Vertex layout (44 bytes):
        //   pos(0,4,8)  uv(12,16)  lmuv(20,24)  normal(28,32,36)  color(40)
        v.position = Vec3(rdF32(data, o), rdF32(data, o + 8), rdF32(data, o + 4));
        v.normal   = Vec3(rdF32(data, o + 28), rdF32(data, o + 36), rdF32(data, o + 32));
        v.uv       = Vec2(rdF32(data, o + 12), rdF32(data, o + 16));
        v.lmuv     = Vec2(rdF32(data, o + 20), rdF32(data, o + 24));


        verts.push_back(v);
    }

    // ---- parse mesh vertices (int indices) ----------------------------------
    int numMeshVerts = 0;
    const u8* mvBase = lumpInfo(data, dataSize, lumps[kLumpMeshVerts], 4, numMeshVerts);
    std::vector<int> meshVerts;
    meshVerts.reserve(numMeshVerts);
    for (int i = 0; i < numMeshVerts; ++i)
    {
        meshVerts.push_back(rdI32(data, static_cast<int>((mvBase - data) + i * 4)));
    }

    // ---- parse faces --------------------------------------------------------
    int numFaces = 0;
    const u8* faceBase = lumpInfo(data, dataSize, lumps[kLumpFaces], kFaceSize, numFaces);
    std::vector<Face> faces;
    faces.reserve(numFaces);
    for (int i = 0; i < numFaces; ++i)
    {
        int o = static_cast<int>((faceBase - data) + i * kFaceSize);
        Face f;
        f.textureIdx = rdI32(data, o);
        f.faceType = rdI32(data, o + 8);
        f.firstVert = rdI32(data, o + 12);
        f.numVerts = rdI32(data, o + 16);
        f.firstMeshVert = rdI32(data, o + 20);
        f.numMeshVerts = rdI32(data, o + 24);
        f.lm_index     = rdI32(data, o + 28);
        f.lm_start[0]  = rdI32(data, o + 32);
        f.lm_start[1]  = rdI32(data, o + 36);
        f.lm_size[0]   = rdI32(data, o + 40);
        f.lm_size[1]   = rdI32(data, o + 44);
        f.patchW = rdI32(data, o + 96);
        f.patchH = rdI32(data, o + 100);
        faces.push_back(f);
    }

    // ---- map each face to its owning BSP model (model 0 = worldspawn) --------
    std::vector<int> faceToModel(numFaces, 0);
    if (collision)
    {
        const auto& models = collision->m_bspModels;
        for (int mi = 1; mi < (int)models.size(); ++mi)
        {
            int first = models[mi].firstFace;
            int count = models[mi].numFaces;
            if (first < 0 || count < 0) continue;
            for (int f = first; f < first + count && f < numFaces; ++f)
                faceToModel[f] = mi;
        }
    }
    else if (out_modelMeshes)
    {
        // Cannot separate entity model faces without the parsed model table.
        out_modelMeshes = nullptr;
    }

    const std::string texDir(textureDir ? textureDir : "");

    // ---- build worldspawn mesh (model 0) ------------------------------------
    Mesh* m = buildBspModelMesh(name, faces, faceToModel, 0, verts, meshVerts,
                                textures, lightmapTextures, texDir, &out_mats);
    if (!m)
    {
        gl::Log::Error("[BSP] no supported geometry in '%s'", path);
        return nullptr;
    }

    // ---- build separate meshes for brush-entity models ----------------------
    if (out_modelMeshes && collision)
    {
        out_modelMeshes->clear();
        const auto& models = collision->m_bspModels;
        for (int mi = 1; mi < (int)models.size(); ++mi)
        {
            std::string modelName = std::string(name) + "_model_" + std::to_string(mi);
            Mesh* modelMesh = buildBspModelMesh(modelName.c_str(), faces, faceToModel, mi,
                                               verts, meshVerts, textures, lightmapTextures, texDir);
            if (modelMesh)
                (*out_modelMeshes)[mi] = modelMesh;
        }
    }

    gl::Log::Info("[BSP] '%s': verts=%u tris=%u surfaces=%u textures=%u lm_pages=%d model_meshes=%u",
                  path,
                  m->vertex_count(), (u32)(m->index_count() / 3),
                  (u32)m->surfaces().size(), (u32)textures.size(), numLightmaps,
                  out_modelMeshes ? (u32)out_modelMeshes->size() : 0u);
    return m;
}

// ============================================================================
// BspEntityInstance / BspDoor — see BspInstance.hpp for the class comments.
// ============================================================================

void BspEntityInstance::init(BspInstance* owner, const BspInstance::Entity& entity, int modelIndex,
                             Mesh* visualMesh)
{
    m_owner = owner;
    m_entity = entity;
    m_modelIndex = modelIndex;
    m_mesh = visualMesh;
    m_materials = visualMesh ? visualMesh->materials() : std::vector<Material*>();
}

void BspEntityInstance::set_offset(const Vec3& o)
{
    m_offset = o;
    set_position(o);
}

float BspEntityInstance::key_float(const char* key, float def) const
{
    auto it = m_entity.keyValues.find(key);
    if (it == m_entity.keyValues.end()) return def;
    return (float)std::atof(it->second.c_str());
}

int BspEntityInstance::key_int(const char* key, int def) const
{
    auto it = m_entity.keyValues.find(key);
    if (it == m_entity.keyValues.end()) return def;
    return std::atoi(it->second.c_str());
}

void BspDoor::setup()
{
    if (!owner() || model_index() < 0) return;
    const auto& models = owner()->bsp_models();
    if (model_index() >= (int)models.size()) return;
    const BspInstance::BspModel& model = models[model_index()];

    float dx = model.maxs.x - model.mins.x;
    float dy = model.maxs.y - model.mins.y;
    float dz = model.maxs.z - model.mins.z;

    float lip = key_float("lip", 8.f);
    float angle = key_float("angle", 0.f);

    if (std::getenv("BSP_DEBUG_DOORS"))
        gl::Log::Warn("[door setup] model=%d angle=%.1f lip=%.1f dx=%.1f dy=%.1f dz=%.1f",
                      model_index(), angle, lip, dx, dy, dz);

    // Quake's own convention (not a dimension guess — an ordinary door is
    // almost always taller than it is thick, so "tallest axis wins" reads
    // nearly every door as a vertical elevator): angle -1 = straight up,
    // -2 = straight down, anything else = horizontal slide direction in
    // degrees. See id's g_utils.c/G_SetMovedir — every real Quake3 door
    // brush follows this, no exceptions.
    if (angle == -1.f)
    {
        m_openOffset = Vec3(0.f, std::max(0.f, dy - lip), 0.f);
    }
    else if (angle == -2.f)
    {
        m_openOffset = Vec3(0.f, -std::max(0.f, dy - lip), 0.f);
    }
    else
    {
        // angle=0/360 must point along +X (verified against q3ctf2's real
        // door brushes: model 19 is dx=64/dz=16 with angle=360 — the wide
        // axis is X, so 0 degrees has to select dx, not dz). The
        // sin/cos-per-axis pairing here was swapped (inherited from the
        // demo's original computeDoorOpenOffset) — harmless-looking until
        // the vertical-vs-horizontal gate above stopped hiding it, since
        // it silently picked each door's THIN axis instead of its wide
        // one (dist-lip landing near zero — "doesn't visibly open").
        float yaw = angle * (3.14159265f / 180.f);
        Vec3 dir(std::cos(yaw), 0.f, std::sin(yaw));
        float dist = (std::fabs(dir.x) > std::fabs(dir.z)) ? dx : dz;
        m_openOffset = dir * std::max(0.f, dist - lip);
    }

    m_speed = key_float("speed", 100.f);
    if (m_speed <= 0.f) m_speed = 100.f;
}

bool BspDoor::near_activator(const Vec3& localPos) const
{
    if (!owner() || model_index() < 0) return false;
    const auto& models = owner()->bsp_models();
    if (model_index() >= (int)models.size()) return false;
    const BspInstance::BspModel& model = models[model_index()];

    Vec3 center = (model.mins + model.maxs) * 0.5f;
    Vec3 dims = model.maxs - model.mins;
    float triggerDist = std::max(dims.x, std::max(dims.y, dims.z)) + 96.f;
    return (center - localPos).length() < triggerDist;
}

void BspDoor::link_teams(const std::vector<BspDoor*>& doors)
{
    std::unordered_map<std::string, std::vector<BspDoor*>> groups;
    for (BspDoor* d : doors)
    {
        auto it = d->entity().keyValues.find("team");
        if (it == d->entity().keyValues.end() || it->second.empty()) continue;
        groups[it->second].push_back(d);
    }
    for (auto& kv : groups)
    {
        if (kv.second.size() < 2) continue; // solo "team" of one — nothing to link
        for (BspDoor* d : kv.second) d->m_team = kv.second;
    }
}

void BspDoor::on_update_mover(float dt)
{
    if (!owner() || model_index() < 0) return;

    m_open = false;
    if (m_activatorNode)
    {
        // World render space (post BspInstance scale/transform) -> this
        // door's own raw map-unit space — the activator is usually the
        // player's camera, which lives in world space (see demos/bsp/
        // main.cpp).
        Vec3 worldPos = m_activatorNode->get_global_position();
        Vec3 localPos = Mat4::Inverse(owner()->get_world_matrix()) * worldPos;

        // Team-linked doors (double doors, a bank of leaves — see
        // link_teams()) open together: near ANY leaf opens the whole
        // group, not just whichever one the player happens to be closest
        // to. Every leaf runs this same check independently each frame,
        // so they all agree without needing a "leader".
        m_open = near_activator(localPos);
        for (BspDoor* mate : m_team)
            if (mate->near_activator(localPos)) { m_open = true; break; }

        static const bool debugDoors = std::getenv("BSP_DEBUG_DOORS") != nullptr;
        if (debugDoors)
        {
            gl::Log::Warn("[door] model=%d localPos=(%.0f,%.0f,%.0f) open=%d team=%zu "
                          "openOff=(%.1f,%.1f,%.1f) offset=(%.1f,%.1f,%.1f)",
                          model_index(), localPos.x, localPos.y, localPos.z, (int)m_open,
                          m_team.size(), m_openOffset.x, m_openOffset.y, m_openOffset.z,
                          offset().x, offset().y, offset().z);
        }
    }

    Vec3 target = m_open ? m_openOffset : m_closedOffset;
    Vec3 delta = target - offset();
    float maxStep = m_speed * dt;
    Vec3 next = (delta.length_squared() > maxStep * maxStep)
                   ? offset() + delta.normalized() * maxStep
                   : target;
    set_offset(next);
}
