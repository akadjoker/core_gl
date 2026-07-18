// MS3DLoader.cpp — MilkShape3D (.ms3d) mesh loader for the coregl scene
// layer. One binary chunk stream (no directory, purely sequential) shared
// by two entry points:
//   AssetManager::load_ms3d_mesh    — geometry only: the joints/keyframes
//                                     chunk at the end of the file, if
//                                     present, is simply never read.
//   SkinnedMesh::load_ms3d/
//   SkinnedMesh::load_animations_ms3d — full skeletal path: bone hierarchy
//                                     + per-vertex bone id + the file's own
//                                     keyframes, dispatched automatically
//                                     from SkinnedMesh::load()/
//                                     load_animations() by sniffing the
//                                     "MS3D" magic (see SkinnedMesh.cpp).
//
// Two things about the format worth calling out, because they're easy to
// get subtly wrong:
//
// 1. MS3D vertices carry exactly one bone ID each (or -1, unweighted) — no
//    blend weights. That's not a shortcut this loader takes; it's the real
//    MS3D v1.x format. GLTFLoader.cpp/IQMLoader.cpp already fall back to
//    the same "weight[0]=1, rest 0" pattern for weightless source
//    vertices, so this isn't new engine behavior, just this format's only
//    behavior.
//
// 2. MS3D keyframes are NOT final local transforms (unlike glTF/IQM/B3D,
//    which is what AnimationClip::sample() expects — see
//    scene/src/AnimationClip.cpp). Each rotation keyframe is an absolute
//    Euler angle and each translation keyframe an absolute vector; the
//    reference composes the animated pose as
//    `finalLocal(t) = bindLocal * deltaMat(t)`, and rotation/translation
//    keyframes are held/interpolated on their OWN independent time axes
//    (different counts/times per joint is legal MS3D). AnimationClip's
//    BoneTrack needs one shared `times` array per bone, so build_bone_track
//    below resamples onto the union of both axes and bakes the composed,
//    decomposed result — see its comment for the exact steps.

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
#include <cstring>

namespace
{

std::string ms3d_stem(const std::string& p)
{
    auto x = p.find_last_of("/\\");
    return x == std::string::npos ? p : p.substr(x + 1);
}
std::string ms3d_dirOf(const std::string& p)
{
    auto sl = p.rfind('/');
    return sl == std::string::npos ? std::string() : p.substr(0, sl + 1);
}

// same fallback list as MD2Loader/MD3Loader/B3DLoader's tryLoadTex: strip
// whatever extension the file recorded (MS3D embeds a Windows path,
// relative or absolute — never trusted directly) and try the engine's own
// image formats against the caller-supplied directory.
gl::Texture* ms3d_tryLoadTex(const std::string& dir, const std::string& ref)
{
    std::string base = ms3d_stem(ref);
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
    // not next to the mesh — try the bare filename too, letting
    // fs::getFilesystem()'s own multi-root search (repo root, build/,
    // build/demos/, build/games/<name>/, ...) find it wherever it actually
    // lives instead of giving up just because it isn't in the mesh's own
    // folder (MS3D's embedded path is a Windows author-time path anyway,
    // never trustworthy — `dir` was already a guess).
    for (const char* e : exts)
    {
        std::string bare = base + e;
        if (fs::getFilesystem().exists(bare.c_str()))
            return assets::AssetManager::instance().loadTexture(base.c_str(), bare.c_str());
    }
    return nullptr;
}

// MS3D's standard roll-pitch-yaw convention: intrinsic X-then-Y-then-Z
// (each axis rotates in the frame already rotated by the previous one),
// i.e. q = qz * qy * qx applied to a vector. This is the well-documented
// convention every MS3D loader uses (see Matrix_SetRotationRadians in
// tmp/apocalyx/ms3d/Matrix.cpp) — if an animated model's limbs twist the
// wrong way once tested, this composition order is the first thing to
// flip.
Quaternion ms3d_eulerToQuat(const Vec3& e)
{
    Quaternion qx = Quaternion::FromAxisAngle(Vec3(1.f, 0.f, 0.f), e.x);
    Quaternion qy = Quaternion::FromAxisAngle(Vec3(0.f, 1.f, 0.f), e.y);
    Quaternion qz = Quaternion::FromAxisAngle(Vec3(0.f, 0.f, 1.f), e.z);
    return qz * qy * qx;
}

Mat4 ms3d_localMat(const Vec3& rotEuler, const Vec3& trans)
{
    return Mat4::Translate(trans) * Mat4(ms3d_eulerToQuat(rotEuler));
}

// matrix -> LocalPose decomposition, same technique Skeleton::bind_pose()
// uses (translation from the last row, scale from column lengths, rotation
// via Quaternion::FromRotation once scale is normalized out).
LocalPose ms3d_decompose(const Mat4& m)
{
    LocalPose p;
    p.position = Vec3(m.c[3][0], m.c[3][1], m.c[3][2]);
    p.scale = Vec3(sqrtf(m.c[0][0] * m.c[0][0] + m.c[0][1] * m.c[0][1] + m.c[0][2] * m.c[0][2]),
                  sqrtf(m.c[1][0] * m.c[1][0] + m.c[1][1] * m.c[1][1] + m.c[1][2] * m.c[1][2]),
                  sqrtf(m.c[2][0] * m.c[2][0] + m.c[2][1] * m.c[2][1] + m.c[2][2] * m.c[2][2]));
    const float sx = p.scale.x > 1e-8f ? 1.f / p.scale.x : 0.f;
    const float sy = p.scale.y > 1e-8f ? 1.f / p.scale.y : 0.f;
    const float sz = p.scale.z > 1e-8f ? 1.f / p.scale.z : 0.f;
    const float r[3][3] = {
        {m.c[0][0] * sx, m.c[1][0] * sy, m.c[2][0] * sz},
        {m.c[0][1] * sx, m.c[1][1] * sy, m.c[2][1] * sz},
        {m.c[0][2] * sx, m.c[1][2] * sy, m.c[2][2] * sz},
    };
    p.rotation = Quaternion::FromRotation(r);
    return p;
}

// ── intermediate parse result: plain data, no engine types yet ──

struct MS3DVertex
{
    Vec3 pos;
    int boneId = -1;
};
struct MS3DTriangle
{
    gl::u16 idx[3];
    Vec3 normal[3];
    Vec2 uv[3];
};
struct MS3DGroup
{
    std::string name;
    std::vector<gl::u16> triIndices;
    int materialIndex = -1;
};
struct MS3DMaterial
{
    Vec3 diffuse{1.f, 1.f, 1.f};
    Vec3 specular{0.f, 0.f, 0.f};
    float shininess = 32.f;
    std::string textureFile;
};
struct MS3DKeyframe
{
    float time;
    Vec3 param;
};
struct MS3DJoint
{
    std::string name;
    std::string parentName;
    Vec3 rotation, translation;
    std::vector<MS3DKeyframe> rotKeys, transKeys;
};

struct MS3DFile
{
    bool ok = false;
    std::vector<MS3DVertex> verts;
    std::vector<MS3DTriangle> tris;
    std::vector<MS3DGroup> groups;
    std::vector<MS3DMaterial> materials;
    std::vector<MS3DJoint> joints; // empty if the file has no skeleton
};

std::string readFixedString(scene::ByteArray& d, gl::u32 len)
{
    std::vector<char> buf(len, 0);
    d.readBytes((gl::u8*)buf.data(), len);
    size_t n = strnlen(buf.data(), len);
    return std::string(buf.data(), n);
}

MS3DFile parseMS3D(scene::ByteArray& data)
{
    MS3DFile f;

    char id[10];
    if (!data.readBytes((gl::u8*)id, 10) || memcmp(id, "MS3D000000", 10) != 0) return f;
    gl::i32 version = 0;
    if (!data.readS32(version) || version < 3 || version > 4) return f;

    gl::u16 nVerts = 0;
    if (!data.readU16(nVerts)) return f;
    f.verts.resize(nVerts);
    for (MS3DVertex& v : f.verts)
    {
        gl::u8 flags = 0;
        data.readU8(flags);
        float p[3] = {0, 0, 0};
        data.readF32Array(p, 3);
        v.pos = Vec3(p[0], p[1], p[2]);
        gl::i8 boneId = -1;
        data.readS8(boneId);
        v.boneId = boneId;
        gl::u8 refCount = 0;
        data.readU8(refCount);
    }

    gl::u16 nTris = 0;
    if (!data.readU16(nTris)) return f;
    f.tris.resize(nTris);
    for (MS3DTriangle& t : f.tris)
    {
        gl::u16 flags = 0;
        data.readU16(flags);
        for (int i = 0; i < 3; ++i) data.readU16(t.idx[i]);
        float normals[9];
        data.readF32Array(normals, 9);
        for (int i = 0; i < 3; ++i)
            t.normal[i] = Vec3(normals[i * 3 + 0], normals[i * 3 + 1], normals[i * 3 + 2]);
        float s[3], v[3];
        data.readF32Array(s, 3);
        data.readF32Array(v, 3);
        // V flipped on load, same convention C3DSLoader.cpp already uses
        for (int i = 0; i < 3; ++i) t.uv[i] = Vec2(s[i], 1.f - v[i]);
        gl::u8 smoothingGroup = 0, groupIndex = 0;
        data.readU8(smoothingGroup);
        data.readU8(groupIndex);
    }

    gl::u16 nGroups = 0;
    if (!data.readU16(nGroups)) return f;
    f.groups.resize(nGroups);
    for (MS3DGroup& g : f.groups)
    {
        gl::u8 flags = 0;
        data.readU8(flags);
        g.name = readFixedString(data, 32);
        gl::u16 nGroupTris = 0;
        data.readU16(nGroupTris);
        g.triIndices.resize(nGroupTris);
        for (gl::u16& ti : g.triIndices) data.readU16(ti);
        gl::i8 matIdx = -1;
        data.readS8(matIdx);
        g.materialIndex = matIdx;
    }

    gl::u16 nMaterials = 0;
    if (!data.readU16(nMaterials)) return f;
    f.materials.resize(nMaterials);
    for (MS3DMaterial& m : f.materials)
    {
        readFixedString(data, 32); // material name — unused (surfaces are addressed by index)
        float ambient[4], diffuse[4], specular[4], emissive[4];
        data.readF32Array(ambient, 4);
        data.readF32Array(diffuse, 4);
        data.readF32Array(specular, 4);
        data.readF32Array(emissive, 4);
        m.diffuse = Vec3(diffuse[0], diffuse[1], diffuse[2]);
        m.specular = Vec3(specular[0], specular[1], specular[2]);
        float shininess = 32.f, transparency = 1.f;
        data.readF32(shininess);
        data.readF32(transparency);
        m.shininess = shininess;
        gl::u8 mode = 0;
        data.readU8(mode);
        m.textureFile = readFixedString(data, 128);
        readFixedString(data, 128); // alphamap — not used yet
    }

    float animFps = 24.f, currentTime = 0.f;
    data.readF32(animFps);
    data.readF32(currentTime);
    gl::i32 totalFrames = 0;
    data.readS32(totalFrames);

    // joints/keyframes chunk: optional (a purely static export omits it —
    // AssetManager::load_ms3d_mesh never gets this far anyway, but
    // SkinnedMesh::load_ms3d needs to know "no skeleton" isn't a parse
    // error, just an empty f.joints)
    gl::u16 numJoints = 0;
    if (data.readU16(numJoints) && numJoints > 0)
    {
        f.joints.resize(numJoints);
        for (MS3DJoint& j : f.joints)
        {
            gl::u8 flags = 0;
            data.readU8(flags);
            j.name = readFixedString(data, 32);
            j.parentName = readFixedString(data, 32);
            float rot[3], trans[3];
            data.readF32Array(rot, 3);
            data.readF32Array(trans, 3);
            j.rotation = Vec3(rot[0], rot[1], rot[2]);
            j.translation = Vec3(trans[0], trans[1], trans[2]);
            gl::u16 numRotKeys = 0, numTransKeys = 0;
            data.readU16(numRotKeys);
            data.readU16(numTransKeys);
            j.rotKeys.resize(numRotKeys);
            for (MS3DKeyframe& k : j.rotKeys)
            {
                float t = 0.f, p[3];
                data.readF32(t);
                data.readF32Array(p, 3);
                k.time = t;
                k.param = Vec3(p[0], p[1], p[2]);
            }
            j.transKeys.resize(numTransKeys);
            for (MS3DKeyframe& k : j.transKeys)
            {
                float t = 0.f, p[3];
                data.readF32(t);
                data.readF32Array(p, 3);
                k.time = t;
                k.param = Vec3(p[0], p[1], p[2]);
            }
        }
    }

    f.ok = true;
    return f;
}

// Builds Mesh geometry shared by both entry points. MS3D doesn't share
// vertices the way an index buffer implies — each triangle corner carries
// its OWN normal/uv, so one source position can legitimately expand into
// several output vertices; no dedup attempted, same as the other
// static loaders here (C3DS, MD3) that face the same per-corner-attribute
// shape. If `outWeights` is non-null, filled 1:1 with the emitted
// vertices (single-bone, weight 1.0 — see the file header comment).
void buildGeometry(const MS3DFile& f, Mesh& mesh,
                   std::vector<SkinnedMesh::VertexWeights>* outWeights)
{
    std::vector<MeshVertex> verts;
    std::vector<gl::u32> indices;
    verts.reserve((size_t)f.tris.size() * 3);
    indices.reserve((size_t)f.tris.size() * 3);
    if (outWeights) outWeights->reserve((size_t)f.tris.size() * 3);

    struct SurfaceRange
    {
        gl::u32 firstIndex, count;
        int materialIndex;
        Vec3 mn, mx;
    };
    std::vector<SurfaceRange> ranges;

    for (const MS3DGroup& g : f.groups)
    {
        gl::u32 firstIndex = (gl::u32)indices.size();
        Vec3 mn(1e30f, 1e30f, 1e30f), mx(-1e30f, -1e30f, -1e30f);
        for (gl::u16 triIdx : g.triIndices)
        {
            if (triIdx >= f.tris.size()) continue;
            const MS3DTriangle& t = f.tris[triIdx];
            for (int c = 0; c < 3; ++c)
            {
                MeshVertex mv;
                const MS3DVertex& sv = f.verts[t.idx[c]];
                mv.position = sv.pos;
                mv.normal = t.normal[c];
                mv.uv = t.uv[c];
                verts.push_back(mv);
                indices.push_back((gl::u32)verts.size() - 1);
                mn = mn.Min(sv.pos);
                mx = mx.Max(sv.pos);
                if (outWeights)
                {
                    SkinnedMesh::VertexWeights vw;
                    gl::u8 bone = (gl::u8)(sv.boneId >= 0 ? sv.boneId : 0);
                    vw.bone[0] = vw.bone[1] = vw.bone[2] = vw.bone[3] = bone;
                    vw.weight[0] = 1.f;
                    vw.weight[1] = vw.weight[2] = vw.weight[3] = 0.f;
                    outWeights->push_back(vw);
                }
            }
        }
        ranges.push_back({firstIndex, (gl::u32)indices.size() - firstIndex, g.materialIndex, mn, mx});
    }

    mesh.set_data(verts.data(), (gl::u32)verts.size(), indices.data(), (gl::u32)indices.size());
    int materialCount = (int)f.materials.size();
    for (const SurfaceRange& r : ranges)
    {
        if (r.count == 0) continue;
        // groups with no material assigned (-1) fall back to slot 0 — same
        // "plain default" fallback AssetManager's other loaders use for
        // unassigned faces
        int slot = (r.materialIndex >= 0 && r.materialIndex < materialCount) ? r.materialIndex : 0;
        // explicit per-surface bounds — the 3-arg add_surface() leaves them
        // default-constructed (0,0,0)-(0,0,0), which SceneOctree/frustum
        // culling reads as "empty, nothing here" and never draws
        mesh.add_surface(r.firstIndex, r.count, slot, BoundingBox(r.mn, r.mx));
    }
    mesh.compute_tangents(); // normals already come from the file; tangents don't
}

// One Material* per MS3D material (or a single default if the file has
// none) — mirrors B3DLoader.cpp's direct-construction approach, not the
// native path's material_infos/buildMaterials indirection (that's only
// used by SkinnedMesh's own .mesh format).
void buildMaterialsMS3D(const MS3DFile& f, const std::string& dir, std::vector<Material*>& out)
{
    if (f.materials.empty())
    {
        Material* mat = new Material();
      //  mat->double_sided = true;
        out.push_back(mat);
        return;
    }
    for (const MS3DMaterial& m : f.materials)
    {
        Material* mat = new Material();
        mat->base_color = m.diffuse;
        mat->specular = (m.specular.x + m.specular.y + m.specular.z) / 3.f;
        mat->shininess = m.shininess;
        if (!m.textureFile.empty())
        {
            mat->diffuse = ms3d_tryLoadTex(dir, m.textureFile);
            if (!mat->diffuse)
                gl::Log::Error("[MS3D] material texture '%s' not found under '%s'",
                               m.textureFile.c_str(), dir.c_str());
        }
       //  mat->double_sided = true;
        out.push_back(mat);
    }
}

// Resolves MS3D's name-based parent references into indices and builds a
// Skeleton — bindLocal per joint from its own rotation/translation (see
// ms3d_localMat), inverseBind from the accumulated world bind matrix
// through the parent chain.
bool buildSkeletonMS3D(const MS3DFile& f, Skeleton& skel)
{
    if (f.joints.empty()) return false;

    std::vector<int> parentIndex(f.joints.size(), -1);
    for (size_t i = 0; i < f.joints.size(); ++i)
    {
        if (f.joints[i].parentName.empty()) continue;
        for (size_t j = 0; j < f.joints.size(); ++j)
            if (f.joints[j].name == f.joints[i].parentName)
            {
                parentIndex[i] = (int)j;
                break;
            }
    }

    std::vector<Mat4> bindWorld(f.joints.size());
    for (size_t i = 0; i < f.joints.size(); ++i)
    {
        Mat4 bindLocal = ms3d_localMat(f.joints[i].rotation, f.joints[i].translation);
        bindWorld[i] =
            parentIndex[i] >= 0 ? bindWorld[(size_t)parentIndex[i]] * bindLocal : bindLocal;
        skel.add_bone(f.joints[i].name.c_str(), parentIndex[i], bindLocal,
                      Mat4::Inverse(bindWorld[i]));
    }
    skel.finalize();
    return true;
}

// held/interpolated sample of one keyframe channel at time t — same rule
// tmp/apocalyx/ms3d/Model.cpp's Model_SetAnimation uses: hold the first key
// before it starts, hold the last key after it ends, lerp between the pair
// straddling t otherwise. Returns the quaternion built from the sampled
// Euler angle (rotation channel) — slerped between keys, not nlerped on
// raw angles, matching the reference's own M3Quaternion::slerp.
Quaternion sampleRotation(const std::vector<MS3DKeyframe>& keys, float t)
{
    if (keys.size() == 1 || t <= keys.front().time) return ms3d_eulerToQuat(keys.front().param);
    if (t >= keys.back().time) return ms3d_eulerToQuat(keys.back().param);
    size_t k1 = (size_t)(std::upper_bound(keys.begin(), keys.end(), t,
                                          [](float v, const MS3DKeyframe& k) { return v < k.time; }) -
                        keys.begin());
    size_t k0 = k1 - 1;
    float span = keys[k1].time - keys[k0].time;
    float f = span > 1e-6f ? (t - keys[k0].time) / span : 0.f;
    return Quaternion::Slerp(ms3d_eulerToQuat(keys[k0].param), ms3d_eulerToQuat(keys[k1].param), f);
}

Vec3 sampleTranslation(const std::vector<MS3DKeyframe>& keys, float t)
{
    if (keys.size() == 1 || t <= keys.front().time) return keys.front().param;
    if (t >= keys.back().time) return keys.back().param;
    size_t k1 = (size_t)(std::upper_bound(keys.begin(), keys.end(), t,
                                          [](float v, const MS3DKeyframe& k) { return v < k.time; }) -
                        keys.begin());
    size_t k0 = k1 - 1;
    float span = keys[k1].time - keys[k0].time;
    float f = span > 1e-6f ? (t - keys[k0].time) / span : 0.f;
    return keys[k0].param + (keys[k1].param - keys[k0].param) * f;
}

// Builds one bone's BoneTrack. See the file header comment (point 2) for
// why this can't be a straight byte-for-byte port of the keyframes: MS3D's
// rotation/translation channels have independent time axes and store
// deltas-on-bind, not final local transforms — both get resolved here into
// the single-time-axis, already-composed form AnimationClip expects.
void buildBoneTrack(int boneIndex, const MS3DJoint& j, const Mat4& bindLocal, BoneTrack& track)
{
    track.bone = boneIndex;
    if (j.rotKeys.empty() && j.transKeys.empty()) return; // no animation on this bone — bind pose stands

    std::vector<float> times;
    times.reserve(j.rotKeys.size() + j.transKeys.size());
    for (const MS3DKeyframe& k : j.rotKeys) times.push_back(k.time);
    for (const MS3DKeyframe& k : j.transKeys) times.push_back(k.time);
    std::sort(times.begin(), times.end());
    times.erase(std::unique(times.begin(), times.end(),
                            [](float a, float b) { return fabsf(a - b) < 1e-5f; }),
               times.end());

    track.times.reserve(times.size());
    track.positions.reserve(times.size());
    track.rotations.reserve(times.size());
    track.scales.reserve(times.size());
    for (float t : times)
    {
        // a channel with zero keys contributes identity to the delta —
        // NOT the joint's own bind rotation/translation (Model_SetAnimation
        // leaves mat_transform at Matrix_LoadIdentity for an untouched
        // channel; falling back to the bind value here would double it up
        // through the bindLocal multiply below)
        Quaternion deltaRot = j.rotKeys.empty() ? Quaternion() : sampleRotation(j.rotKeys, t);
        Vec3 deltaTrans = j.transKeys.empty() ? Vec3(0.f, 0.f, 0.f) : sampleTranslation(j.transKeys, t);
        Mat4 deltaMat = Mat4::Translate(deltaTrans) * Mat4(deltaRot);
        Mat4 finalLocal = bindLocal * deltaMat;
        LocalPose p = ms3d_decompose(finalLocal);
        track.times.push_back(t);
        track.positions.push_back(p.position);
        track.rotations.push_back(p.rotation);
        track.scales.push_back(p.scale);
    }
}

} // namespace

// ── static entry point ──

Mesh* assets::AssetManager::load_ms3d_mesh(const char* name, const char* path,
                                           std::vector<Material*>& out_mats,
                                           const char* textureDir)
{
    if (!name || !path) return nullptr;
    if (Mesh* existing = getMesh(name)) return existing;

    scene::ByteArray data;
    if (!fs::getFilesystem().readFile(path, data))
    {
        gl::Log::Error("[MS3D] cannot read '%s'", path);
        return nullptr;
    }
    data.resetCursor();

    MS3DFile f = parseMS3D(data);
    if (!f.ok)
    {
        gl::Log::Error("[MS3D] '%s' is not a valid MilkShape3D file", path);
        return nullptr;
    }

    Mesh* mesh = createMesh(name);
    buildGeometry(f, *mesh, nullptr);
    mesh->upload(); // Scene::collect_instance() skips any mesh that isn't — see C3DSLoader/B3DLoader
    gl::Log::Info("[MS3D] '%s' parsed: verts=%zu tris=%zu groups=%zu -> mesh: verts=%u idx=%u "
                  "surfaces=%zu",
                  path, f.verts.size(), f.tris.size(), f.groups.size(),
                  (gl::u32)mesh->vertices().size(), (gl::u32)mesh->indices().size(),
                  mesh->surfaces().size());

    std::string dir = textureDir && *textureDir ? std::string(textureDir) : ms3d_dirOf(path);
    if (!dir.empty() && dir.back() != '/') dir += '/';
    buildMaterialsMS3D(f, dir, out_mats);

    gl::Log::Info("[MS3D] '%s' — %u tris, %zu materials (static)", path,
                  (gl::u32)f.tris.size(), out_mats.size());
    return mesh;
}

// ── skinned/animated entry points ──

bool SkinnedMesh::load_ms3d(const char* path)
{
    scene::ByteArray data;
    if (!fs::getFilesystem().readFile(path, data))
    {
        gl::Log::Error("[MS3D] cannot read '%s'", path);
        return false;
    }
    data.resetCursor();

    MS3DFile f = parseMS3D(data);
    if (!f.ok)
    {
        gl::Log::Error("[MS3D] '%s' is not a valid MilkShape3D file", path);
        return false;
    }
    if (f.joints.empty())
    {
        gl::Log::Error("[MS3D] '%s' has no bones — use AssetManager::load_ms3d_mesh() for static "
                       "geometry",
                       path);
        return false;
    }

    buildSkeletonMS3D(f, m_skeleton);
    buildGeometry(f, m_mesh, &m_weights);

    std::string dir = ms3d_dirOf(path);
    buildMaterialsMS3D(f, dir, m_materials);

    gl::Log::Info("[MS3D] '%s': tris=%u bones=%d materials=%zu", path, (gl::u32)f.tris.size(),
                  m_skeleton.bone_count(), m_materials.size());
    m_loaded = true;
    return true;
}

bool SkinnedMesh::load_animations_ms3d(const char* path)
{
    if (m_skeleton.empty())
    {
        gl::Log::Error("[MS3D] load_animations_ms3d('%s') called before a skeleton is loaded",
                       path);
        return false;
    }

    scene::ByteArray data;
    if (!fs::getFilesystem().readFile(path, data))
    {
        gl::Log::Error("[MS3D] cannot read '%s'", path);
        return false;
    }
    data.resetCursor();

    MS3DFile f = parseMS3D(data);
    if (!f.ok || f.joints.empty())
    {
        gl::Log::Error("[MS3D] '%s' has no animation data", path);
        return false;
    }

    AnimationClip* clip = new AnimationClip();
    clip->set_name(ms3d_stem(path));
    float duration = 0.f;
    for (const MS3DJoint& j : f.joints)
    {
        int boneIndex = m_skeleton.find_bone(j.name.c_str());
        if (boneIndex < 0) continue; // joint in the anim file but not in the bound skeleton
        BoneTrack track;
        buildBoneTrack(boneIndex, j, m_skeleton.bone(boneIndex).bindLocal, track);
        if (track.times.empty()) continue;
        duration = std::max(duration, track.times.back());
        clip->tracks().push_back(std::move(track));
    }
    clip->set_duration(duration);
    m_clips.push_back(clip);

    gl::Log::Info("[MS3D] '%s': clip '%s', %zu tracks, %.2fs", path, clip->name().c_str(),
                  clip->tracks().size(), duration);
    return true;
}
