// BSPLoader.cpp — Quake 3 BSP (IBSP v46) mesh loader for the coregl scene layer.
 // Imports polygon / mesh faces directly and tessellates Bezier patches.
// Vertices are converted from Z-up to Y-up.  One Material per texture group.
 

#include "scene/Mesh.hpp"
#include "scene/Material.hpp"
#include "scene/AssetManager.hpp"
#include "scene/Math.hpp"
#include "scene/Filesystem.hpp"
#include "coregl/gl_log.hpp"

#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

namespace
{

// ---- Quake 3 BSP constants --------------------------------------------------
constexpr int kQ3Ident = 1347633737; // "IBSP"
constexpr int kQ3Version = 46;       // Quake 3
constexpr int kNumLumps = 17;
constexpr int kLumpTextures = 1;
constexpr int kLumpVertices = 10;
constexpr int kLumpMeshVerts = 11;
constexpr int kLumpFaces = 13;

constexpr int kFacePolygon = 1;
constexpr int kFacePatch = 2;
constexpr int kFaceMesh = 3;
constexpr int kLumpLightmaps = 14;

constexpr int kTextureSize = 72; // sizeof(bsp texture entry)
constexpr int kVertexSize = 44;  // sizeof(bsp vertex)
constexpr int kFaceSize = 104;   // sizeof(bsp face)

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
    mv.uv = bv.uv;
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

// Try loading a texture from `textureDir` + `name` with several extensions.
// Q3 BSP texture names are like "textures/base_floor/clanggrate"; we join
// directly with textureDir (the map root, e.g. "assets/bsp/oa_rpg3dm2/").
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
    // Fallback — AssetManager returns checkerboard so the scene keeps rendering.
    return assets::AssetManager::instance().loadTexture(name.c_str(),
                                                        (textureDir + name + ".tga").c_str(),
                                                        true);
}

} // namespace

Mesh* assets::AssetManager::load_bsp_mesh(const char* name, const char* path,
                     std::vector<Material*>& out_mats,
                     const char* textureDir)
{
    if (!name || !path) return nullptr;

    // Already loaded?
    Mesh* existing = getMesh(name);
    if (existing) return existing;

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
    constexpr int kLmSize = 128 * 128 * 3;
    int numLightmaps = 0;
    const u8* lmBase = lumpInfo(data, dataSize, lumps[kLumpLightmaps], kLmSize, numLightmaps);
    std::vector<gl::Texture*> lightmapTextures;
    lightmapTextures.reserve(numLightmaps);
    for (int i = 0; i < numLightmaps; ++i)
    {
        const u8* src = lmBase + i * kLmSize;
        std::vector<u8> rgba(128 * 128 * 4);
        for (int p = 0; p < 128 * 128; ++p)
        {
            rgba[p * 4 + 0] = src[p * 3 + 0];
            rgba[p * 4 + 1] = src[p * 3 + 1];
            rgba[p * 4 + 2] = src[p * 3 + 2];
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

    // ---- group faces by texture index (preserve insertion order) ------------
    std::unordered_map<int, std::vector<int>> group;
    std::vector<int> order;
    for (int i = 0; i < numFaces; ++i)
    {
        int t = faces[i].textureIdx;
        if (group.find(t) == group.end()) order.push_back(t);
        group[t].push_back(i);
    }

    // ---- build engine geometry ---------------------------------------------
    Mesh* m = createMesh(name);
    if (!m) return nullptr;
    std::vector<MeshVertex> outVerts;
    std::vector<Vec2> outLmUvs; // parallel: lightmap UV per vertex
    std::vector<u32> outIndices;
    outVerts.reserve(numVerts * 2);
    outLmUvs.reserve(numVerts * 2);
    outIndices.reserve(meshVerts.size());

    const std::string texDir(textureDir ? textureDir : "");

    struct SurfDef { u32 start, count; int slot; BoundingBox bb; };
    std::vector<SurfDef> surfDefs;

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

        // Compute surface bounding box.
        Vec3 mn(MaxFloat, MaxFloat, MaxFloat);
        Vec3 mx(-MaxFloat, -MaxFloat, -MaxFloat);
        for (u32 i = startIndex; i < (u32)outIndices.size(); ++i)
        {
            const Vec3& p = outVerts[outIndices[i]].position;
            mn = mn.Min(p);
            mx = mx.Max(p);
        }

        // Create material for this texture group.
        std::string texName = (ti >= 0 && ti < (int)textures.size() && !textures[ti].empty())
                                  ? textures[ti]
                                  : ("surf_" + std::to_string(surfIdx));
        Material* mat = new Material();
        gl::Texture* tex = tryLoadTex(texDir, texName);
        mat->diffuse = tex;
        mat->double_sided = false; // winding swap already fixes CW→CCW
        mat->lightmapped = true;        mat->detail_scale = 1.0f; // lightmap, not tiled detail
        // Assign the lightmap page used by the first face in this group.
        if (!faceList.empty())
        {
            int lmIdx = faces[faceList[0]].lm_index;
            if (lmIdx >= 0 && lmIdx < numLightmaps)
                mat->detail = lightmapTextures[lmIdx];
        }
        out_mats.push_back(mat);

        surfDefs.push_back({startIndex, indexCount, surfIdx, BoundingBox(mn, mx)});
        ++surfIdx;
    }

    if (outVerts.empty() || outIndices.empty())
    {
        gl::Log::Error("[BSP] no supported geometry in '%s'", path);
        return nullptr;
    }

    // Q3 BSP faces use CW winding; OpenGL expects CCW. Flip every triangle.
    // for (size_t i = 0; i + 2 < outIndices.size(); i += 3)
    //     std::swap(outIndices[i + 1], outIndices[i + 2]);

    // set_data first (clears surfaces), then add_surface, then upload.
    m->set_data(outVerts.data(), static_cast<u32>(outVerts.size()), outIndices.data(),
                static_cast<u32>(outIndices.size()));
    for (const SurfDef& s : surfDefs)
        m->add_surface(s.start, s.count, s.slot, s.bb);
    m->set_lightmap_uvs(outLmUvs.data(), static_cast<u32>(outLmUvs.size()));
    // Mesh takes ownership from here — freed with it in AssetManager::clear()/
    // ~Mesh(); out_mats stays a valid view for the caller (texture tweaks,
    // etc.) but must not be deleted by it.
    m->set_owned_materials(out_mats);
    m->upload();

    gl::Log::Info("[BSP] '%s': verts=%u tris=%u surfaces=%u textures=%u lm_uvs=%d lm_pages=%d", path,
                  (u32)outVerts.size(), (u32)(outIndices.size() / 3), (u32)surfDefs.size(),
                  (u32)textures.size(), (int)outLmUvs.size(), numLightmaps);
    return m;
}
