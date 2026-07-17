#include "scene/AssetManager.hpp"
#include "scene/ByteArray.hpp"
#include "scene/Filesystem.hpp"
#include "scene/Material.hpp"
#include "scene/Mesh.hpp"
#include "scene/MorphAnimation.hpp"
#include <cstdio>
#include <unordered_map>

namespace
{
constexpr int kMd2Ident = 844121161; // "IDP2"
constexpr int kMd2Version = 8;

struct Md2Tri
{
    u16 vi[3];
    u16 ti[3];
};
struct VKey
{
    u16 vi, ti;
    bool operator==(const VKey& o) const { return vi == o.vi && ti == o.ti; }
};
struct VKeyHash
{
    size_t operator()(const VKey& k) const { return ((size_t)k.vi << 16) ^ k.ti; }
};

// face-normal accumulation, matches Mesh::compute_normals' approach
std::vector<Vec3> computeNormals(const std::vector<Vec3>& pos, const std::vector<u32>& idx)
{
    std::vector<Vec3> nrm(pos.size(), Vec3(0, 0, 0));
    for (size_t i = 0; i + 2 < idx.size(); i += 3)
    {
        u32 a = idx[i], b = idx[i + 1], c = idx[i + 2];
        Vec3 fn = Vec3::Cross(pos[b] - pos[a], pos[c] - pos[a]);
        nrm[a] += fn;
        nrm[b] += fn;
        nrm[c] += fn;
    }
    for (Vec3& v : nrm)
    {
        float l2 = v.length_squared();
        v = (l2 > 1e-12f) ? v * (1.0f / sqrtf(l2)) : Vec3(0, 1, 0);
    }
    return nrm;
}

std::string stem(const std::string& p)
{
    auto x = p.rfind('/');
    return x == std::string::npos ? p : p.substr(x + 1);
}

std::string dirOf(const std::string& p)
{
    auto sl = p.rfind('/');
    return sl == std::string::npos ? std::string() : p.substr(0, sl + 1);
}

gl::Texture* tryLoadSkin(const std::string& dir, const std::string& name)
{
    std::string base = stem(name);
    auto dot = base.rfind('.');
    if (dot != std::string::npos) base = base.substr(0, dot);

    const char* exts[] = {".png", ".tga", ".jpg", ".jpeg", ".bmp"};
    for (const char* e : exts)
    {
        std::string fullPath = dir + base + e;
        if (fs::getFilesystem().exists(fullPath.c_str()))
            return assets::AssetManager::instance().loadTexture(base.c_str(), fullPath.c_str());
    }
    return assets::AssetManager::instance().loadTexture(base.c_str(), (dir + base + ".tga").c_str());
}
} // namespace

Mesh* assets::AssetManager::load_md2_mesh(const char* name, const char* path, MorphKeyframes& outAnim,
                                          std::vector<Material*>& outMats, const char* textureDir)
{
    if (!name || !path) return nullptr;

    Mesh* existing = getMesh(name);
    if (existing) return existing;

    scene::ByteArray b;
    if (!fs::getFilesystem().readFile(path, b))
    {
        fprintf(stderr, "[MD2] could not open: %s\n", path);
        return nullptr;
    }

    auto rdI32 = [&](gl::i32& v) { b.readS32(v); };
    auto rdF32 = [&](gl::f32& v) { b.readF32(v); };

    gl::i32 ident = 0, version = 0;
    rdI32(ident);
    rdI32(version);
    gl::i32 skinW = 0, skinH = 0;
    rdI32(skinW);
    rdI32(skinH);
    gl::i32 frameSize = 0;
    rdI32(frameSize);
    gl::i32 numSkins = 0, numVerts = 0, numUV = 0, numTris = 0, numGLCmds = 0, numFrames = 0;
    rdI32(numSkins);
    rdI32(numVerts);
    rdI32(numUV);
    rdI32(numTris);
    rdI32(numGLCmds);
    rdI32(numFrames);
    gl::i32 ofsSkins = 0, ofsUV = 0, ofsTris = 0, ofsFrames = 0;
    rdI32(ofsSkins);
    rdI32(ofsUV);
    rdI32(ofsTris);
    rdI32(ofsFrames);

    if (ident != kMd2Ident || version != kMd2Version || numFrames <= 0 || numVerts <= 0 ||
        numTris <= 0 || numUV <= 0 || skinW <= 0 || skinH <= 0)
    {
        fprintf(stderr, "[MD2] bad header: %s\n", path);
        return nullptr;
    }

    std::string firstSkin;
    if (numSkins > 0)
    {
        b.seek((gl::u32)ofsSkins);
        char sk[64] = {};
        b.readBytes((gl::u8*)sk, 64);
        firstSkin = sk;
    }

    std::vector<std::pair<gl::i16, gl::i16>> uvs((size_t)numUV);
    b.seek((gl::u32)ofsUV);
    for (int i = 0; i < numUV; ++i)
    {
        gl::i16 u = 0, v = 0;
        b.readS16(u);
        b.readS16(v);
        uvs[(size_t)i] = {u, v};
    }

    std::vector<Md2Tri> tris((size_t)numTris);
    b.seek((gl::u32)ofsTris);
    for (int i = 0; i < numTris; ++i)
    {
        for (int k = 0; k < 3; ++k)
        {
            gl::u16 v = 0;
            b.readU16(v);
            tris[(size_t)i].vi[k] = v;
        }
        for (int k = 0; k < 3; ++k)
        {
            gl::u16 v = 0;
            b.readU16(v);
            tris[(size_t)i].ti[k] = v;
        }
    }

    // dedup (vi,ti) -> render vertex, remember the source MD2 vertex index
    std::unordered_map<VKey, u32, VKeyHash> map;
    std::vector<u16> srcVert;
    std::vector<MeshVertex> baseVerts;
    std::vector<u32> indices;
    // k iterates 2,1,0 (not 0,1,2): the Z-up->Y-up position swizzle below
    // flips handedness (swapping two axes of a right-handed system makes
    // it left-handed), so the original winding needs reversing to stay
    // front-facing in the new space — same reasoning as MD3Loader's
    // triangle-index flip, and it's what makes computeNormals() below
    // point outward instead of into the mesh.
    for (const Md2Tri& tri : tris)
        for (int k = 2; k >= 0; --k)
        {
            VKey key{tri.vi[k], tri.ti[k]};
            auto it = map.find(key);
            u32 ri;
            if (it == map.end())
            {
                ri = (u32)baseVerts.size();
                map.emplace(key, ri);
                srcVert.push_back(key.vi);
                MeshVertex v{};
                v.uv = Vec2((float)uvs[key.ti].first / (float)skinW,
                            1.0f - (float)uvs[key.ti].second / (float)skinH);
                v.tangent = Vec4(1, 0, 0, 1);
                baseVerts.push_back(v);
            }
            else ri = it->second;
            indices.push_back(ri);
        }

    // frames: decompress positions (u8 * scale + translate), MD2 (Z-up) -> Y-up
    const size_t nRender = baseVerts.size();
    outAnim.framePos.assign((size_t)numFrames, std::vector<Vec3>(nRender));
    std::vector<Vec3> raw((size_t)numVerts);
    b.seek((gl::u32)ofsFrames);
    for (int f = 0; f < numFrames; ++f)
    {
        Vec3 scale, trans;
        rdF32(scale.x);
        rdF32(scale.y);
        rdF32(scale.z);
        rdF32(trans.x);
        rdF32(trans.y);
        rdF32(trans.z);
        char fname[16] = {};
        b.readBytes((gl::u8*)fname, 16);
        for (int v = 0; v < numVerts; ++v)
        {
            gl::u8 x = 0, y = 0, z = 0, ni = 0;
            b.readU8(x);
            b.readU8(y);
            b.readU8(z);
            b.readU8(ni); // normal index (Quake2 precomputed table) — unused, recomputed below
            Vec3 p(x * scale.x + trans.x, y * scale.y + trans.y, z * scale.z + trans.z);
            raw[(size_t)v] = Vec3(p.x, p.z, p.y); // Z-up -> Y-up
        }
        for (size_t i = 0; i < nRender; ++i) outAnim.framePos[(size_t)f][i] = raw[srcVert[i]];
    }

    outAnim.frameNrm.resize((size_t)numFrames);
    for (int f = 0; f < numFrames; ++f) outAnim.frameNrm[(size_t)f] = computeNormals(outAnim.framePos[(size_t)f], indices);

    // material: MD2's baked-in skin, if present
    outMats.clear();
    std::string dir = textureDir && *textureDir ? std::string(textureDir) : dirOf(path);
    if (!dir.empty() && dir.back() != '/') dir += '/';
    Material* mat = new Material();
    if (!firstSkin.empty()) mat->diffuse = tryLoadSkin(dir, firstSkin);
    outMats.push_back(mat);

    // frame 0 pose, ready for the animator to take over via update_vertices()
    for (size_t i = 0; i < nRender; ++i)
    {
        baseVerts[i].position = outAnim.framePos[0][i];
        baseVerts[i].normal = outAnim.frameNrm[0][i];
    }
    // bounds = union of every vertex across ALL frames (not just frame 0) —
    // see MD3Loader.cpp's load_md3_mesh for why: a frame-0-only box either
    // clips parts of the animation, or wrongly culls the whole surface up
    // close once the near plane passes the box before the real geometry.
    Vec3 mn = outAnim.framePos[0][0], mx = mn;
    for (const std::vector<Vec3>& frame : outAnim.framePos)
        for (const Vec3& p : frame)
        {
            mn = mn.Min(p);
            mx = mx.Max(p);
        }
    BoundingBox allFramesBounds(mn, mx);

    Mesh* mesh = createMesh(name);
    mesh->set_data(baseVerts.data(), (u32)baseVerts.size(), indices.data(), (u32)indices.size());
    mesh->add_surface(0, (u32)indices.size(), 0, allFramesBounds);
    // Mesh takes ownership from here — freed with it in AssetManager::clear()/
    // ~Mesh(); outMats stays a valid view for the caller (texture tweaks,
    // etc.) but must not be deleted by it.
    mesh->set_owned_materials(outMats);
    mesh->upload_dynamic();

    printf("[MD2] '%s': verts=%u tris=%u frames=%d\n", path, (unsigned)nRender,
           (unsigned)(indices.size() / 3), numFrames);
    return mesh;
}
