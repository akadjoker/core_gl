#include "scene/AssetManager.hpp"
#include "scene/ByteArray.hpp"
#include "scene/Filesystem.hpp"
#include "scene/Material.hpp"
#include "scene/Mesh.hpp"
#include "scene/MorphAnimation.hpp"
#include <cmath>
#include <cstdio>

namespace
{
constexpr int kMd3Ident = 860898377; // "IDP3"
constexpr int kMd3Version = 15;
constexpr float kPI = 3.14159265358979323846f;

// MD3 is Z-up like MD2; the engine is Y-up. Same (x,z,y) swap used by
// MD2Loader, applied to positions, tag origins, and (component-wise) each
// row of a tag's axis matrix.
Vec3 zupToYup(const Vec3& v) { return Vec3(v.x, v.z, v.y); }

std::string readFixedStr(scene::ByteArray& b, int n)
{
    std::vector<char> buf((size_t)n);
    b.readBytes((gl::u8*)buf.data(), (gl::u32)n);
    std::string out;
    for (int i = 0; i < n && buf[(size_t)i]; ++i) out.push_back(buf[(size_t)i]);
    return out;
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

gl::Texture* tryLoadSkin(const std::string& dir, const std::string& ref)
{
    std::string base = stem(ref);
    auto dot = base.rfind('.');
    if (dot != std::string::npos) base = base.substr(0, dot);
    if (base.empty()) return nullptr;

    const char* exts[] = {".png", ".tga", ".jpg", ".jpeg", ".bmp"};
    for (const char* e : exts)
    {
        std::string fullPath = dir + base + e;
        if (fs::getFilesystem().exists(fullPath.c_str()))
            return assets::AssetManager::instance().loadTexture(base.c_str(), fullPath.c_str());
    }
    return nullptr;
}

struct SurfInfo
{
    gl::u32 base;
    gl::i32 numFrames, numShaders, numVerts, numTris;
    gl::i32 ofsTris, ofsShaders, ofsST, ofsXYZ, ofsEnd;
    std::string name;
};
} // namespace

Mesh* assets::AssetManager::load_md3_mesh(const char* name, const char* path, MorphKeyframes& outAnim,
                                          std::vector<Material*>& outMats, MorphTags* outTags,
                                          const char* textureDir)
{
    if (!name || !path) return nullptr;

    Mesh* existing = getMesh(name);
    if (existing) return existing;

    scene::ByteArray b;
    if (!fs::getFilesystem().readFile(path, b))
    {
        fprintf(stderr, "[MD3] could not open: %s\n", path);
        return nullptr;
    }

    auto rdI32 = [&](gl::i32& v) { b.readS32(v); };

    gl::i32 ident = 0, version = 0;
    rdI32(ident);
    rdI32(version);
    readFixedStr(b, 64); // name
    gl::i32 flags = 0;
    rdI32(flags);
    gl::i32 numFrames = 0, numTags = 0, numSurfaces = 0, numSkins = 0;
    rdI32(numFrames);
    rdI32(numTags);
    rdI32(numSurfaces);
    rdI32(numSkins);
    gl::i32 ofsFrames = 0, ofsTags = 0, ofsSurfaces = 0, ofsEndHdr = 0;
    rdI32(ofsFrames);
    rdI32(ofsTags);
    rdI32(ofsSurfaces);
    rdI32(ofsEndHdr);

    if (ident != kMd3Ident || version != kMd3Version || numFrames <= 0 || numSurfaces <= 0 ||
        ofsSurfaces <= 0)
    {
        fprintf(stderr, "[MD3] bad header: %s\n", path);
        return nullptr;
    }

    // ── tags: numTags per frame, each { name[64]; origin[3]; axis[3][3] } ──
    if (outTags && numTags > 0)
    {
        b.seek((gl::u32)ofsTags);
        outTags->names.resize((size_t)numTags);
        outTags->perFrame.assign((size_t)numFrames, std::vector<MorphTagFrame>((size_t)numTags));
        for (int f = 0; f < numFrames; ++f)
            for (int t = 0; t < numTags; ++t)
            {
                std::string name = readFixedStr(b, 64);
                if (f == 0) outTags->names[(size_t)t] = name;

                Vec3 origin(0, 0, 0);
                b.readF32(origin.x);
                b.readF32(origin.y);
                b.readF32(origin.z);

                Vec3 raw[3]; // as read from file, still Z-up, NOT swizzled yet
                for (int r = 0; r < 3; ++r)
                {
                    Vec3 row(0, 0, 0);
                    b.readF32(row.x);
                    b.readF32(row.y);
                    b.readF32(row.z);
                    raw[r] = row;
                }
                // MD3's axis[3] are the tag's local X/Y/Z basis vectors
                // *expressed in parent space* — columns of the rotation
                // matrix R_zup, not rows (cross-checked against
                // tmp/apocalyx's MD3Transform::fillArray, a native Z-up
                // engine with no Y-up swizzle: it builds a column-major
                // GL matrix where column i = the i-th 3-float group read
                // from the file — i.e. R[row][col] = axis[col][row],
                // confirming this transpose relationship).
                //
                // Converting THAT matrix into our Y-up space is NOT just
                // swizzling each axis vector's components — vertex data
                // gets the same (x,z,y) swap (zupToYup), so keeping
                // rotate(v) consistent between the two spaces needs the
                // full conjugation R_yup = P * R_zup * P (P = the y/z swap,
                // its own inverse). That works out to: swizzle each axis
                // vector's components AND swap which axis vector fills
                // column 1 vs column 2 — not just the swizzle alone (which
                // is what the previous "fix" still got wrong: right
                // direction, missing the column swap).
                Vec3 c0 = zupToYup(raw[0]);
                Vec3 c1 = zupToYup(raw[2]);
                Vec3 c2 = zupToYup(raw[1]);
                float m[3][3] = {{c0.x, c1.x, c2.x},
                                  {c0.y, c1.y, c2.y},
                                  {c0.z, c1.z, c2.z}};

                MorphTagFrame& tf = outTags->perFrame[(size_t)f][(size_t)t];
                tf.origin = zupToYup(origin);
                tf.rotation = Quaternion::FromRotation(m);
            }

        printf("[MD3] '%s' tags:", path);
        for (const std::string& tagName : outTags->names) printf(" %s", tagName.c_str());
        printf("\n");
    }

    // ── surfaces ──
    std::vector<SurfInfo> surfs;
    size_t totalVerts = 0, totalIdx = 0;
    gl::u32 off = (gl::u32)ofsSurfaces;
    for (int i = 0; i < numSurfaces; ++i)
    {
        b.seek(off);
        SurfInfo si;
        si.base = off;
        gl::i32 sid = 0;
        rdI32(sid);
        si.name = readFixedStr(b, 64);
        gl::i32 sflags = 0;
        rdI32(sflags);
        rdI32(si.numFrames);
        rdI32(si.numShaders);
        rdI32(si.numVerts);
        rdI32(si.numTris);
        rdI32(si.ofsTris);
        rdI32(si.ofsShaders);
        rdI32(si.ofsST);
        rdI32(si.ofsXYZ);
        rdI32(si.ofsEnd);
        if (sid != kMd3Ident || si.numFrames != numFrames || si.numVerts <= 0 || si.numTris <= 0 ||
            si.ofsEnd <= 0)
        {
            fprintf(stderr, "[MD3] bad surface in: %s\n", path);
            return nullptr;
        }
        surfs.push_back(si);
        totalVerts += (size_t)si.numVerts;
        totalIdx += (size_t)si.numTris * 3u;
        off += (gl::u32)si.ofsEnd;
    }
    if (totalVerts == 0 || totalIdx == 0)
    {
        fprintf(stderr, "[MD3] no geometry: %s\n", path);
        return nullptr;
    }

    std::vector<MeshVertex> baseVerts(totalVerts);
    std::vector<u32> indices;
    indices.reserve(totalIdx);
    outAnim.framePos.assign((size_t)numFrames, std::vector<Vec3>(totalVerts));
    outAnim.frameNrm.assign((size_t)numFrames, std::vector<Vec3>(totalVerts));

    std::string dir = textureDir && *textureDir ? std::string(textureDir) : dirOf(path);
    if (!dir.empty() && dir.back() != '/') dir += '/';
    outMats.clear();

    size_t vbase = 0;
    for (size_t si = 0; si < surfs.size(); ++si)
    {
        const SurfInfo& info = surfs[si];

        std::string shader;
        if (info.numShaders > 0)
        {
            b.seek(info.base + (gl::u32)info.ofsShaders);
            shader = readFixedStr(b, 64);
        }

        b.seek(info.base + (gl::u32)info.ofsST);
        for (int v = 0; v < info.numVerts; ++v)
        {
            Vec2 uv(0, 0);
            b.readF32(uv.x);
            b.readF32(uv.y);
            baseVerts[vbase + (size_t)v].uv = uv;
            baseVerts[vbase + (size_t)v].tangent = Vec4(1, 0, 0, 1);
        }

        b.seek(info.base + (gl::u32)info.ofsTris);
        for (int t = 0; t < info.numTris; ++t)
        {
            gl::i32 A = 0, B = 0, C = 0;
            rdI32(A);
            rdI32(B);
            rdI32(C);
            // winding flip to match the engine's front-face convention
            indices.push_back((u32)(vbase + (size_t)A));
            indices.push_back((u32)(vbase + (size_t)B));
            indices.push_back((u32)(vbase + (size_t)C));
        }

        b.seek(info.base + (gl::u32)info.ofsXYZ);
        for (int f = 0; f < numFrames; ++f)
            for (int v = 0; v < info.numVerts; ++v)
            {
                gl::i16 sx = 0, sy = 0, sz = 0;
                b.readS16(sx);
                b.readS16(sy);
                b.readS16(sz);
                Vec3 p((float)sx / 64.0f, (float)sy / 64.0f, (float)sz / 64.0f);
                gl::u8 lat = 0, lng = 0;
                b.readU8(lat);
                b.readU8(lng);
                float la = (float)lat * (2.0f * kPI / 255.0f);
                float ln = (float)lng * (2.0f * kPI / 255.0f);
                Vec3 n(cosf(la) * sinf(ln), sinf(la) * sinf(ln), cosf(ln));
                outAnim.framePos[(size_t)f][vbase + (size_t)v] = zupToYup(p);
                outAnim.frameNrm[(size_t)f][vbase + (size_t)v] = zupToYup(n);
            }

        Material* mat = new Material();
        std::string ref = shader.empty() ? info.name : shader;
        if (!ref.empty()) mat->diffuse = tryLoadSkin(dir, ref);
        outMats.push_back(mat);

        vbase += (size_t)info.numVerts;
    }

    // frame 0 pose, ready for the animator to take over via update_vertices()
    for (size_t i = 0; i < totalVerts; ++i)
    {
        baseVerts[i].position = outAnim.framePos[0][i];
        baseVerts[i].normal = outAnim.frameNrm[0][i];
    }

    // bounds = union of every vertex across ALL frames, not just frame 0 —
    // the mesh deforms every frame via update_vertices(), so a frame-0-only
    // (or, worse, the add_surface(...)-without-bounds default: a zero-size
    // box at the origin) box either clips parts of the animation or — up
    // close — has the near plane sweep through that single origin point
    // before the actual geometry, wrongly culling the whole surface.
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
    u32 start = 0;
    for (size_t si = 0; si < surfs.size(); ++si)
    {
        u32 cnt = (u32)surfs[si].numTris * 3u;
        mesh->add_surface(start, cnt, (int)si, allFramesBounds);
        start += cnt;
    }
    // Mesh takes ownership from here — freed with it in AssetManager::clear()/
    // ~Mesh(); outMats stays a valid view for the caller (texture tweaks,
    // etc.) but must not be deleted by it.
    mesh->set_owned_materials(outMats);
    mesh->upload_dynamic();

    printf("[MD3] '%s': verts=%u tris=%u frames=%d surfaces=%u tags=%d\n", path,
           (unsigned)totalVerts, (unsigned)(totalIdx / 3), numFrames, (unsigned)surfs.size(),
           numTags);
    return mesh;
}
