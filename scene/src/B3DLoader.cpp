// B3DLoader.cpp — Blitz3D (.b3d) mesh loader for the coregl scene layer.
// Two entry points share the same BB3D/TEXS/BRUS/NODE/MESH/VRTS/TRIS/BONE/
// ANIM/KEYS chunk format:
//   AssetManager::load_b3d_mesh   — static geometry only: each NODE's own
//                                   transform is baked straight into its
//                                   vertices, BONE/ANIM/KEYS are skipped.
//   SkinnedMesh::load_b3d/
//   SkinnedMesh::load_animations_b3d — full skeletal path: bone hierarchy,
//                                   per-vertex skin weights and the file's
//                                   embedded animation, dispatched
//                                   automatically from SkinnedMesh::load()/
//                                   load_animations() (see SkinnedMesh.cpp)
//                                   by sniffing the "BB3D" magic.
// Ported from tmp/core/src/B3DLoader.cpp (BinaryStream/Skeleton/Animation)
// onto this engine's ByteArray + Skeleton/AnimationClip/SkinnedMesh types.

#include "scene/AnimationClip.hpp"
#include "scene/AssetManager.hpp"
#include "scene/ByteArray.hpp"
#include "scene/Filesystem.hpp"
#include "scene/Material.hpp"
#include "scene/Mesh.hpp"
#include "scene/Skeleton.hpp"
#include "scene/SkinnedMesh.hpp"
#include <coregl/gl_log.hpp>
#include <algorithm>
#include <cmath>

namespace
{

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

// same fallback list as MD2Loader/MD3Loader's tryLoadSkin: strip whatever
// extension the file recorded and try the engine's own image formats.
gl::Texture* tryLoadTex(const std::string& dir, const std::string& ref)
{
    std::string base = stem(ref);
    auto dot = base.rfind('.');
    if (dot != std::string::npos) base = base.substr(0, dot);
    if (base.empty()) return nullptr;

    const char* exts[] = {".png", ".tga", ".jpg", ".jpeg", ".bmp"};
    for (const char* e : exts)
    {
        std::string full = dir + base + e;
        if (fs::getFilesystem().exists(full.c_str()))
            return assets::AssetManager::instance().loadTexture(base.c_str(), full.c_str());
    }
    return nullptr;
}

// Blitz3D quaternions are left-handed; matching this engine's right-handed
// convention just needs w negated (same fix tmp/core's B3D/IQM loaders use).
Quaternion qnorm(float x, float y, float z, float w)
{
    Quaternion q(x, y, z, w);
    q.normalize();
    return q;
}

std::string readTag(scene::ByteArray& b)
{
    char t[4] = {};
    b.readBytes((gl::u8*)t, 4);
    return std::string(t, 4);
}
std::string readCStrB3D(scene::ByteArray& b)
{
    std::string o;
    gl::u8 c = 0;
    while (b.readU8(c) && c) o.push_back((char)c);
    return o;
}

// nested-chunk bookkeeping: each chunk is [4-char tag][i32 length][payload];
// `push` remembers where the payload ends so `remaining` can drive loops
// without knowing the chunk's internal structure.
struct ChunkStack
{
    std::vector<gl::u32> ends;
    void push(scene::ByteArray& b, gl::u32 len) { ends.push_back(b.cursor() + len); }
    void pop(scene::ByteArray& b)
    {
        if (!ends.empty())
        {
            b.setCursor(ends.back());
            ends.pop_back();
        }
    }
    gl::u32 remaining(const scene::ByteArray& b) const
    {
        return ends.empty() ? 0 : (ends.back() > b.cursor() ? ends.back() - b.cursor() : 0);
    }
};

bool readHeader(scene::ByteArray& data, ChunkStack& stack)
{
    char root[4] = {};
    if (!data.readBytes((gl::u8*)root, 4) || std::string(root, 4) != "BB3D") return false;
    gl::i32 len = 0;
    if (!data.readS32(len)) return false;
    stack.push(data, (gl::u32)len);
    gl::i32 version = 0;
    data.readS32(version);
    return true;
}

// ---------------------------------------------------------------------
// Static path: bakes every NODE's accumulated transform into its own
// MESH's vertices, ignores BONE/ANIM/KEYS entirely.
// ---------------------------------------------------------------------
struct StaticSurf
{
    u32 firstIndex = 0, indexCount = 0;
    int brushId = -1;
};

struct B3DStaticCtx
{
    scene::ByteArray& b;
    ChunkStack stack;
    std::vector<MeshVertex> verts;
    std::vector<u32> indices;
    std::vector<StaticSurf> surfaces;
    std::vector<std::string> texNames;
    std::vector<int> brushTex0;

    void readVRTS(const Mat4& xform)
    {
        Mat3 nmat = Mat3::NormalMatrix(xform);
        gl::i32 flags = 0, numUV = 0, uvSize = 0;
        b.readS32(flags);
        b.readS32(numUV);
        b.readS32(uvSize);
        int stride = 12;
        if (flags & 1) stride += 12;
        if (flags & 2) stride += 16;
        stride += numUV * uvSize * 4;
        int count = stride > 0 ? (int)(stack.remaining(b) / (gl::u32)stride) : 0;
        for (int i = 0; i < count; ++i)
        {
            MeshVertex v{};
            Vec3 pos, nrm(0, 1, 0);
            b.readF32(pos.x);
            b.readF32(pos.y);
            b.readF32(pos.z);
            if (flags & 1)
            {
                b.readF32(nrm.x);
                b.readF32(nrm.y);
                b.readF32(nrm.z);
            }
            if (flags & 2)
            {
                float c0, c1, c2, c3;
                b.readF32(c0);
                b.readF32(c1);
                b.readF32(c2);
                b.readF32(c3);
            }
            Vec2 uv(0, 0);
            for (int t = 0; t < numUV; ++t)
            {
                float u = 0, vv = 0;
                if (uvSize >= 1) b.readF32(u);
                if (uvSize >= 2) b.readF32(vv);
                for (int k = 2; k < uvSize; ++k)
                {
                    float skip;
                    b.readF32(skip);
                }
                if (t == 0) uv = Vec2(u, vv);
            }
            v.position = xform * pos;
            v.normal = (nmat * nrm).normalized();
            v.uv = uv;
            v.tangent = Vec4(1, 0, 0, 1);
            verts.push_back(v);
        }
    }

    void readTRIS(u32 vertexStart)
    {
        gl::i32 brushId = 0;
        b.readS32(brushId);
        StaticSurf s;
        s.brushId = brushId;
        s.firstIndex = (u32)indices.size();
        int triCount = (int)(stack.remaining(b) / 12);
        for (int i = 0; i < triCount; ++i)
        {
            gl::i32 i0 = 0, i1 = 0, i2 = 0;
            b.readS32(i0);
            b.readS32(i1);
            b.readS32(i2);
            indices.push_back(vertexStart + (u32)i0);
            indices.push_back(vertexStart + (u32)i1);
            indices.push_back(vertexStart + (u32)i2);
        }
        s.indexCount = (u32)indices.size() - s.firstIndex;
        if (s.indexCount > 0) surfaces.push_back(s);
    }

    void parseNode(const Mat4& parentGlobal)
    {
        readCStrB3D(b); // node name, unused for static geometry
        Vec3 pos, scale(1, 1, 1);
        b.readF32(pos.x);
        b.readF32(pos.y);
        b.readF32(pos.z);
        b.readF32(scale.x);
        b.readF32(scale.y);
        b.readF32(scale.z);
        float rw = 1, rx = 0, ry = 0, rz = 0;
        b.readF32(rw);
        b.readF32(rx);
        b.readF32(ry);
        b.readF32(rz);
        Quaternion rot = qnorm(rx, ry, rz, -rw);
        Mat4 global = parentGlobal * (Mat4::Translate(pos) * Mat4(rot) * Mat4::Scale(scale.x, scale.y, scale.z));

        while (stack.remaining(b) > 0)
        {
            std::string tag = readTag(b);
            gl::i32 len = 0;
            b.readS32(len);
            stack.push(b, (gl::u32)len);

            if (tag == "MESH")
            {
                u32 nodeVertexStart = (u32)verts.size();
                gl::i32 meshBrush = 0;
                b.readS32(meshBrush); // ignored: TRIS carries its own brush id
                while (stack.remaining(b) > 0)
                {
                    std::string mtag = readTag(b);
                    gl::i32 mlen = 0;
                    b.readS32(mlen);
                    stack.push(b, (gl::u32)mlen);
                    if (mtag == "VRTS") readVRTS(global);
                    else if (mtag == "TRIS") readTRIS(nodeVertexStart);
                    stack.pop(b);
                }
            }
            else if (tag == "NODE")
                parseNode(global);
            // BONE/ANIM/KEYS: no skeleton in the static path — skipped via stack.pop below

            stack.pop(b);
        }
    }
};

} // namespace

Mesh* assets::AssetManager::load_b3d_mesh(const char* name, const char* path,
                                          std::vector<Material*>& out_mats,
                                          const char* textureDir)
{
    if (!name || !path) return nullptr;
    Mesh* existing = getMesh(name);
    if (existing) return existing;

    scene::ByteArray data;
    if (!fs::getFilesystem().readFile(path, data))
    {
        gl::Log::Error("[B3D] cannot read '%s'", path);
        return nullptr;
    }
    data.resetCursor();

    B3DStaticCtx ctx{data, {}, {}, {}, {}, {}, {}};
    if (!readHeader(data, ctx.stack))
    {
        gl::Log::Error("[B3D] bad root chunk in '%s'", path);
        return nullptr;
    }

    std::string dir = textureDir && *textureDir ? std::string(textureDir) : dirOf(path);
    if (!dir.empty() && dir.back() != '/') dir += '/';

    while (ctx.stack.remaining(data) > 0)
    {
        std::string tag = readTag(data);
        gl::i32 len = 0;
        data.readS32(len);
        ctx.stack.push(data, (gl::u32)len);

        if (tag == "TEXS")
        {
            while (ctx.stack.remaining(data) > 0)
            {
                ctx.texNames.push_back(readCStrB3D(data));
                gl::i32 flags = 0, blend = 0;
                data.readS32(flags);
                data.readS32(blend);
                float f;
                data.readF32(f);
                data.readF32(f);
                data.readF32(f);
                data.readF32(f);
                data.readF32(f);
            }
        }
        else if (tag == "BRUS")
        {
            gl::i32 nTex = 0;
            data.readS32(nTex);
            while (ctx.stack.remaining(data) > 0)
            {
                readCStrB3D(data);
                float f;
                data.readF32(f);
                data.readF32(f);
                data.readF32(f);
                data.readF32(f);
                data.readF32(f);
                gl::i32 blend = 0, fx = 0;
                data.readS32(blend);
                data.readS32(fx);
                int tex0 = -1;
                for (int i = 0; i < nTex; ++i)
                {
                    gl::i32 tid = -1;
                    data.readS32(tid);
                    if (i == 0) tex0 = tid;
                }
                ctx.brushTex0.push_back(tex0);
            }
        }
        else if (tag == "NODE")
            ctx.parseNode(Mat4::Identity());

        ctx.stack.pop(data);
    }

    if (ctx.verts.empty() || ctx.indices.empty())
    {
        gl::Log::Error("[B3D] no geometry in '%s'", path);
        return nullptr;
    }

    out_mats.clear();
    for (const StaticSurf& s : ctx.surfaces)
    {
        Material* mat = new Material();
        if (s.brushId >= 0 && s.brushId < (int)ctx.brushTex0.size())
        {
            int ts = ctx.brushTex0[(size_t)s.brushId];
            if (ts >= 0 && ts < (int)ctx.texNames.size())
                mat->diffuse = tryLoadTex(dir, ctx.texNames[(size_t)ts]);
        }
        out_mats.push_back(mat);
    }

    Mesh* mesh = createMesh(name);
    mesh->set_data(ctx.verts.data(), (u32)ctx.verts.size(), ctx.indices.data(), (u32)ctx.indices.size());
    for (size_t i = 0; i < ctx.surfaces.size(); ++i)
        mesh->add_surface(ctx.surfaces[i].firstIndex, ctx.surfaces[i].indexCount, (int)i);
    mesh->compute_tangents();
    mesh->set_owned_materials(out_mats);
    mesh->upload();

    gl::Log::Info("[B3D] '%s' (static): verts=%u idx=%u surfaces=%zu", path, (u32)ctx.verts.size(),
                  (u32)ctx.indices.size(), ctx.surfaces.size());
    return mesh;
}

namespace
{

// ---------------------------------------------------------------------
// Animated/skeletal path: full port of tmp/core/src/B3DLoader.cpp onto
// Skeleton/AnimationClip/SkinnedMesh — bones, per-vertex skin weights and
// the embedded animation.
// ---------------------------------------------------------------------

// keeps the 4 largest weights per vertex, same scheme as tmp/core's WeightSlots
struct WeightSlots
{
    int ids[4] = {-1, -1, -1, -1};
    float w[4] = {0, 0, 0, 0};
    void add(int bone, float wt)
    {
        if (bone < 0 || wt <= 0.0f) return;
        if (wt > w[0])
        {
            ids[3] = ids[2]; w[3] = w[2];
            ids[2] = ids[1]; w[2] = w[1];
            ids[1] = ids[0]; w[1] = w[0];
            ids[0] = bone; w[0] = wt;
        }
        else if (wt > w[1])
        {
            ids[3] = ids[2]; w[3] = w[2];
            ids[2] = ids[1]; w[2] = w[1];
            ids[1] = bone; w[1] = wt;
        }
        else if (wt > w[2])
        {
            ids[3] = ids[2]; w[3] = w[2];
            ids[2] = bone; w[2] = wt;
        }
        else if (wt > w[3])
        {
            ids[3] = bone; w[3] = wt;
        }
    }
};

struct RawKey
{
    float time;
    Vec3 v;
};
struct RawRotKey
{
    float time;
    Quaternion q;
};
struct BoneAnim
{
    Vec3 bindPos = Vec3(0, 0, 0);
    Quaternion bindRot;
    Vec3 bindScale = Vec3(1, 1, 1);
    std::vector<RawKey> pos;
    std::vector<RawRotKey> rot;
    std::vector<RawKey> scale;
};
struct PendingSurface
{
    u32 indexStart = 0, indexCount = 0;
    int brushId = -1;
};

struct B3DAnimCtx
{
    scene::ByteArray& b;
    ChunkStack stack;
    Skeleton& skel;
    std::vector<BoneAnim> boneAnim;
    std::vector<MeshVertex> verts;
    std::vector<u32> indices;
    std::vector<WeightSlots> weights;
    std::vector<PendingSurface> surfaces;
    std::vector<std::string> texNames;
    std::vector<int> brushTex0;
    int animFrames; // default member initializers would make this a non-aggregate under C++11;
    float animFps;  // always set explicitly by the aggregate-init call site instead

    int addBone(const std::string& name, int parent, const Vec3& p, const Vec3& sc, const Quaternion& r)
    {
        Mat4 local = Mat4::Translate(p) * Mat4(r) * Mat4::Scale(sc.x, sc.y, sc.z);
        int idx = skel.bone_count();
        skel.add_bone(name.c_str(), parent, local, Mat4::Identity()); // inverse_bind filled after the whole tree is known
        BoneAnim ba;
        ba.bindPos = p;
        ba.bindRot = r;
        ba.bindScale = sc;
        boneAnim.push_back(ba);
        return idx;
    }

    void readVRTS()
    {
        gl::i32 flags = 0, numUV = 0, uvSize = 0;
        b.readS32(flags);
        b.readS32(numUV);
        b.readS32(uvSize);
        int stride = 12;
        if (flags & 1) stride += 12;
        if (flags & 2) stride += 16;
        stride += numUV * uvSize * 4;
        int count = stride > 0 ? (int)(stack.remaining(b) / (gl::u32)stride) : 0;
        for (int i = 0; i < count; ++i)
        {
            MeshVertex v{};
            v.position = Vec3(0, 0, 0);
            b.readF32(v.position.x);
            b.readF32(v.position.y);
            b.readF32(v.position.z);
            v.normal = Vec3(0, 1, 0);
            if (flags & 1)
            {
                b.readF32(v.normal.x);
                b.readF32(v.normal.y);
                b.readF32(v.normal.z);
            }
            if (flags & 2)
            {
                float c0, c1, c2, c3;
                b.readF32(c0);
                b.readF32(c1);
                b.readF32(c2);
                b.readF32(c3);
            }
            v.uv = Vec2(0, 0);
            for (int t = 0; t < numUV; ++t)
            {
                float u = 0, vv = 0;
                if (uvSize >= 1) b.readF32(u);
                if (uvSize >= 2) b.readF32(vv);
                for (int k = 2; k < uvSize; ++k)
                {
                    float skip;
                    b.readF32(skip);
                }
                if (t == 0) v.uv = Vec2(u, vv);
            }
            v.tangent = Vec4(1, 0, 0, 1);
            verts.push_back(v);
            weights.push_back(WeightSlots());
        }
    }

    void readTRIS(u32 nodeVertexStart)
    {
        gl::i32 brushId = 0;
        b.readS32(brushId);
        PendingSurface surf;
        surf.brushId = brushId;
        surf.indexStart = (u32)indices.size();
        int triCount = (int)(stack.remaining(b) / 12);
        for (int i = 0; i < triCount; ++i)
        {
            gl::i32 i0 = 0, i1 = 0, i2 = 0;
            b.readS32(i0);
            b.readS32(i1);
            b.readS32(i2);
            indices.push_back(nodeVertexStart + (u32)i0);
            indices.push_back(nodeVertexStart + (u32)i1);
            indices.push_back(nodeVertexStart + (u32)i2);
        }
        surf.indexCount = (u32)indices.size() - surf.indexStart;
        if (surf.indexCount > 0) surfaces.push_back(surf);
    }

    void parseNode(int parentBone)
    {
        std::string nodeName = readCStrB3D(b);
        Vec3 pos, scale(1, 1, 1);
        b.readF32(pos.x);
        b.readF32(pos.y);
        b.readF32(pos.z);
        b.readF32(scale.x);
        b.readF32(scale.y);
        b.readF32(scale.z);
        float rw = 1, rx = 0, ry = 0, rz = 0;
        b.readF32(rw);
        b.readF32(rx);
        b.readF32(ry);
        b.readF32(rz);
        Quaternion rot = qnorm(rx, ry, rz, -rw);
        int myBone = addBone(nodeName, parentBone, pos, scale, rot);
        u32 nodeVertexStart = 0;

        while (stack.remaining(b) > 0)
        {
            std::string tag = readTag(b);
            gl::i32 len = 0;
            b.readS32(len);
            stack.push(b, (gl::u32)len);

            if (tag == "MESH")
            {
                nodeVertexStart = (u32)verts.size();
                gl::i32 meshBrush = 0;
                b.readS32(meshBrush); // ignored: TRIS carries its own brush id
                while (stack.remaining(b) > 0)
                {
                    std::string mtag = readTag(b);
                    gl::i32 mlen = 0;
                    b.readS32(mlen);
                    stack.push(b, (gl::u32)mlen);
                    if (mtag == "VRTS") readVRTS();
                    else if (mtag == "TRIS") readTRIS(nodeVertexStart);
                    stack.pop(b);
                }
            }
            else if (tag == "BONE")
            {
                while (stack.remaining(b) > 0)
                {
                    gl::i32 localV = 0;
                    float wt = 0;
                    b.readS32(localV);
                    b.readF32(wt);
                    int g = (int)nodeVertexStart + localV;
                    if (g >= 0 && g < (int)weights.size()) weights[(size_t)g].add(myBone, wt);
                }
            }
            else if (tag == "ANIM")
            {
                gl::i32 animFlags = 0;
                b.readS32(animFlags);
                gl::i32 frames = 0;
                b.readS32(frames);
                animFrames = frames;
                b.readF32(animFps);
                if (animFps <= 0.0f) animFps = 25.0f;
            }
            else if (tag == "KEYS")
            {
                gl::i32 flags = 0;
                b.readS32(flags);
                BoneAnim& d = boneAnim[(size_t)myBone];
                while (stack.remaining(b) > 0)
                {
                    gl::i32 frame = 0;
                    b.readS32(frame);
                    float t = (float)frame;
                    if (flags & 1)
                    {
                        Vec3 p;
                        b.readF32(p.x);
                        b.readF32(p.y);
                        b.readF32(p.z);
                        d.pos.push_back({t, p});
                    }
                    if (flags & 2)
                    {
                        Vec3 s;
                        b.readF32(s.x);
                        b.readF32(s.y);
                        b.readF32(s.z);
                        d.scale.push_back({t, s});
                    }
                    if (flags & 4)
                    {
                        float qw = 1, qx = 0, qy = 0, qz = 0;
                        b.readF32(qw);
                        b.readF32(qx);
                        b.readF32(qy);
                        b.readF32(qz);
                        d.rot.push_back({t, qnorm(qx, qy, qz, -qw)});
                    }
                }
            }
            else if (tag == "NODE")
                parseNode(myBone);

            stack.pop(b);
        }
    }
};

// full chunk-tree parse shared by load_b3d() and load_animations_b3d():
// bone hierarchy (with a self-contained, already inverse-bound Skeleton),
// per-vertex skin weights, geometry and the file's own embedded animation
// keys. Neither caller needs the raw BoneAnim tracks directly — only the
// finished AnimationClip(s), built once here by buildB3DClip.
struct B3DSkeletalResult
{
    bool ok = false;
    Skeleton skel;
    std::vector<MeshVertex> verts;
    std::vector<u32> indices;
    std::vector<WeightSlots> weights;
    std::vector<PendingSurface> surfaces;
    std::vector<std::string> texNames;
    std::vector<int> brushTex0;
    std::vector<BoneAnim> boneAnim;
    int animFrames = 0;
    float animFps = 25.0f;
};

B3DSkeletalResult parseB3DSkeletal(const char* path)
{
    B3DSkeletalResult r;

    scene::ByteArray data;
    if (!fs::getFilesystem().readFile(path, data))
    {
        gl::Log::Error("[B3D] cannot read '%s'", path);
        return r;
    }
    data.resetCursor();

    ChunkStack rootStack;
    if (!readHeader(data, rootStack))
    {
        gl::Log::Error("[B3D] bad root chunk in '%s'", path);
        return r;
    }

    B3DAnimCtx ctx{data, rootStack, r.skel, {}, {}, {}, {}, {}, {}, {}, 0, 25.0f};

    while (ctx.stack.remaining(data) > 0)
    {
        std::string tag = readTag(data);
        gl::i32 len = 0;
        data.readS32(len);
        ctx.stack.push(data, (gl::u32)len);

        if (tag == "TEXS")
        {
            while (ctx.stack.remaining(data) > 0)
            {
                ctx.texNames.push_back(readCStrB3D(data));
                gl::i32 flags = 0, blend = 0;
                data.readS32(flags);
                data.readS32(blend);
                float f;
                data.readF32(f);
                data.readF32(f);
                data.readF32(f);
                data.readF32(f);
                data.readF32(f);
            }
        }
        else if (tag == "BRUS")
        {
            gl::i32 nTex = 0;
            data.readS32(nTex);
            while (ctx.stack.remaining(data) > 0)
            {
                readCStrB3D(data);
                float f;
                data.readF32(f);
                data.readF32(f);
                data.readF32(f);
                data.readF32(f);
                data.readF32(f);
                gl::i32 blend = 0, fx = 0;
                data.readS32(blend);
                data.readS32(fx);
                int tex0 = -1;
                for (int i = 0; i < nTex; ++i)
                {
                    gl::i32 tid = -1;
                    data.readS32(tid);
                    if (i == 0) tex0 = tid;
                }
                ctx.brushTex0.push_back(tex0);
            }
        }
        else if (tag == "NODE")
            ctx.parseNode(-1);

        ctx.stack.pop(data);
    }

    if (ctx.verts.empty() || ctx.indices.empty())
    {
        gl::Log::Error("[B3D] no geometry in '%s'", path);
        return r;
    }

    // inverse_bind = inverse(accumulated global bind transform); B3D's own
    // NODE nesting already guarantees parents precede children, so a
    // single forward pass is enough. Skeleton only takes inverseBind
    // through add_bone, so the tree is rebuilt once the binds are known.
    int n = r.skel.bone_count();
    if (n > 0)
    {
        std::vector<Mat4> global((size_t)n);
        for (int i = 0; i < n; ++i)
        {
            const Bone& bone = r.skel.bone(i);
            global[(size_t)i] =
                (bone.parent >= 0) ? global[(size_t)bone.parent] * bone.bindLocal : bone.bindLocal;
        }
        Skeleton rebuilt;
        for (int i = 0; i < n; ++i)
        {
            const Bone& bone = r.skel.bone(i);
            rebuilt.add_bone(bone.name.c_str(), bone.parent, bone.bindLocal, global[(size_t)i].inverted());
        }
        rebuilt.finalize();
        r.skel = std::move(rebuilt);
    }

    r.verts = std::move(ctx.verts);
    r.indices = std::move(ctx.indices);
    r.weights = std::move(ctx.weights);
    r.surfaces = std::move(ctx.surfaces);
    r.texNames = std::move(ctx.texNames);
    r.brushTex0 = std::move(ctx.brushTex0);
    r.boneAnim = std::move(ctx.boneAnim);
    r.animFrames = ctx.animFrames;
    r.animFps = ctx.animFps;
    r.ok = true;
    return r;
}

// linear search for the surrounding pair + lerp/nlerp, same scheme as
// AnimationClip::sample()/GLTFLoader.cpp's sampleVec3Channel/sampleQuatChannel
Vec3 sampleVecKeys(const std::vector<RawKey>& keys, float t, const Vec3& fallback)
{
    if (keys.empty()) return fallback;
    if (keys.size() == 1 || t <= keys.front().time) return keys.front().v;
    if (t >= keys.back().time) return keys.back().v;
    size_t k1 = (size_t)(std::upper_bound(keys.begin(), keys.end(), t,
                                          [](float tt, const RawKey& k) { return tt < k.time; }) -
                         keys.begin());
    size_t k0 = k1 - 1;
    float span = keys[k1].time - keys[k0].time;
    float f = span > 1e-6f ? (t - keys[k0].time) / span : 0.f;
    return keys[k0].v + (keys[k1].v - keys[k0].v) * f;
}
template <typename T>
void sortKeysByTime(std::vector<T>& keys)
{
    std::sort(keys.begin(), keys.end(), [](const T& a, const T& b) { return a.time < b.time; });
}

Quaternion sampleRotKeys(const std::vector<RawRotKey>& keys, float t, const Quaternion& fallback)
{
    if (keys.empty()) return fallback;
    if (keys.size() == 1 || t <= keys.front().time) return keys.front().q;
    if (t >= keys.back().time) return keys.back().q;
    size_t k1 = (size_t)(std::upper_bound(keys.begin(), keys.end(), t,
                                          [](float tt, const RawRotKey& k) { return tt < k.time; }) -
                         keys.begin());
    size_t k0 = k1 - 1;
    float span = keys[k1].time - keys[k0].time;
    float f = span > 1e-6f ? (t - keys[k0].time) / span : 0.f;
    return Quaternion::Nlerp(keys[k0].q, keys[k1].q, f);
}

// builds the file's single embedded animation ("default") as one
// AnimationClip, bone index i == boneAnim[i] == skel.bone(i). BoneTrack
// drives position/rotation/scale off ONE shared time array (AnimationClip::
// sample() indexes all three in lockstep — see GLTFLoader.cpp's
// buildClipTracks for the same constraint), so the file's independent
// pos/rot/scale key times are unioned and each channel resampled onto it.
AnimationClip* buildB3DClip(std::vector<BoneAnim>& boneAnim, int animFrames, float animFps)
{
    if (animFrames <= 1) return nullptr;

    AnimationClip* clip = new AnimationClip();
    clip->set_name("default");
    float ticksPerSecond = animFps > 0.0f ? animFps : 25.0f;
    clip->set_duration((float)(animFrames - 1) / ticksPerSecond);

    for (size_t i = 0; i < boneAnim.size(); ++i)
    {
        BoneAnim& src = boneAnim[i];
        if (src.pos.empty() && src.rot.empty() && src.scale.empty()) continue;

        sortKeysByTime(src.pos);
        sortKeysByTime(src.rot);
        sortKeysByTime(src.scale);

        std::vector<float> unified;
        for (const RawKey& k : src.pos) unified.push_back(k.time);
        for (const RawRotKey& k : src.rot) unified.push_back(k.time);
        for (const RawKey& k : src.scale) unified.push_back(k.time);
        std::sort(unified.begin(), unified.end());
        unified.erase(
            std::unique(unified.begin(), unified.end(), [](float a, float b) { return fabsf(a - b) < 1e-4f; }),
            unified.end());
        if (unified.empty()) continue;

        BoneTrack tr;
        tr.bone = (gl::i32)i;
        for (float t : unified)
        {
            tr.times.push_back(t / ticksPerSecond);
            tr.positions.push_back(sampleVecKeys(src.pos, t, src.bindPos));
            tr.rotations.push_back(sampleRotKeys(src.rot, t, src.bindRot));
            tr.scales.push_back(sampleVecKeys(src.scale, t, src.bindScale));
        }
        clip->tracks().push_back(std::move(tr));
    }

    if (clip->tracks().empty())
    {
        delete clip;
        return nullptr;
    }
    return clip;
}

} // namespace

bool SkinnedMesh::load_b3d(const char* path)
{
    B3DSkeletalResult r = parseB3DSkeletal(path);
    if (!r.ok) return false;
    if (r.skel.empty())
    {
        gl::Log::Error("[B3D] '%s' has no bones — use AssetManager::load_b3d_mesh() for static geometry",
                       path);
        return false;
    }

    m_skeleton = std::move(r.skel);

    // normalize weights -> VertexWeights
    m_weights.resize(r.weights.size());
    for (size_t i = 0; i < r.verts.size(); ++i)
    {
        const WeightSlots& ws = r.weights[i];
        float sum = ws.w[0] + ws.w[1] + ws.w[2] + ws.w[3];
        SkinnedMesh::VertexWeights& vw = m_weights[i];
        if (sum <= 1e-8f)
        {
            vw.bone[0] = vw.bone[1] = vw.bone[2] = vw.bone[3] = 0;
            vw.weight[0] = 1.f;
            vw.weight[1] = vw.weight[2] = vw.weight[3] = 0.f;
            continue;
        }
        int b0 = ws.ids[0] >= 0 ? ws.ids[0] : 0;
        int b1 = ws.ids[1] >= 0 ? ws.ids[1] : b0;
        int b2 = ws.ids[2] >= 0 ? ws.ids[2] : b0;
        int b3 = ws.ids[3] >= 0 ? ws.ids[3] : b0;
        vw.bone[0] = (gl::u8)Clamp(b0, 0, 255);
        vw.bone[1] = (gl::u8)Clamp(b1, 0, 255);
        vw.bone[2] = (gl::u8)Clamp(b2, 0, 255);
        vw.bone[3] = (gl::u8)Clamp(b3, 0, 255);
        vw.weight[0] = ws.w[0] / sum;
        vw.weight[1] = ws.w[1] / sum;
        vw.weight[2] = ws.w[2] / sum;
        vw.weight[3] = ws.w[3] / sum;
    }

    // materials: one per TRIS surface, brush -> first texture, same
    // convention as the static path (and tmp/core's original loader)
    std::string dir = dirOf(path);
    if (r.surfaces.empty())
    {
        PendingSurface whole;
        whole.indexStart = 0;
        whole.indexCount = (u32)r.indices.size();
        whole.brushId = -1;
        r.surfaces.push_back(whole);
    }
    for (const PendingSurface& s : r.surfaces)
    {
        Material* mat = new Material();
        if (s.brushId >= 0 && s.brushId < (int)r.brushTex0.size())
        {
            int ts = r.brushTex0[(size_t)s.brushId];
            if (ts >= 0 && ts < (int)r.texNames.size())
                mat->diffuse = tryLoadTex(dir, r.texNames[(size_t)ts]);
        }
        m_materials.push_back(mat);
    }

    m_mesh.set_data(r.verts.data(), (u32)r.verts.size(), r.indices.data(), (u32)r.indices.size());
    for (size_t i = 0; i < r.surfaces.size(); ++i)
        m_mesh.add_surface(r.surfaces[i].indexStart, r.surfaces[i].indexCount, (int)i);
    m_mesh.compute_tangents();

    if (AnimationClip* clip = buildB3DClip(r.boneAnim, r.animFrames, r.animFps)) m_clips.push_back(clip);

    gl::Log::Info("[B3D] '%s': verts=%u idx=%u bones=%d surfaces=%zu clips=%zu", path, (u32)r.verts.size(),
                  (u32)r.indices.size(), m_skeleton.bone_count(), r.surfaces.size(), m_clips.size());
    m_loaded = true;
    return true;
}

// Anim-only .b3d (a separate export sharing the same skeleton, e.g.
// walk.b3d/idle.b3d for one character): re-parses the file's own NODE tree
// for ANIM/KEYS data, binding tracks to the ALREADY-loaded skeleton by bone
// name — mirrors SkinnedMesh::load_animations_gltf's contract exactly.
bool SkinnedMesh::load_animations_b3d(const char* path)
{
    if (m_skeleton.empty())
    {
        gl::Log::Error("[B3D] load_animations_b3d('%s') called before a skeleton is loaded", path);
        return false;
    }

    B3DSkeletalResult r = parseB3DSkeletal(path);
    if (!r.ok || r.skel.empty()) return false;

    AnimationClip* clip = buildB3DClip(r.boneAnim, r.animFrames, r.animFps);
    if (!clip)
    {
        gl::Log::Error("[B3D] '%s' has no usable animation", path);
        return false;
    }

    // rebind each track's bone index by name against THIS skeleton
    auto& tracks = clip->tracks();
    for (size_t i = 0; i < tracks.size();)
    {
        int idx = (tracks[i].bone >= 0 && tracks[i].bone < r.skel.bone_count())
                     ? m_skeleton.find_bone(r.skel.bone(tracks[i].bone).name.c_str())
                     : -1;
        if (idx < 0)
            tracks.erase(tracks.begin() + (long)i);
        else
        {
            tracks[i].bone = idx;
            ++i;
        }
    }

    if (tracks.empty())
    {
        gl::Log::Error("[B3D] '%s' — no clip's bones matched the loaded skeleton", path);
        delete clip;
        return false;
    }

    gl::Log::Info("[B3D] clip '%s' (from '%s') — %.2fs, %zu tracks", clip->name().c_str(), path,
                  clip->duration(), tracks.size());
    m_clips.push_back(clip);
    return true;
}
