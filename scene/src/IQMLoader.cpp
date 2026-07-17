// IQMLoader.cpp — Inter-Quake Model (.iqm v2) loader, dispatched
// automatically from SkinnedMesh::load()/load_animations() (see
// SkinnedMesh.cpp) by sniffing the "INTERQUAKEMODEL" magic — every
// existing call site (AssetManager::loadSkinnedMesh/loadAnimation, every
// character demo) works unchanged whether the file is the native
// .mesh/.anim format, glTF or IQM.
//
// Ported from tmp/core/src/IQMLoader.cpp (BinaryStream/Skeleton/Animation)
// onto this engine's ByteArray + Skeleton/AnimationClip/SkinnedMesh types.
// Per-frame joint poses (channelmask/offset/scale) are decoded into
// BoneTrack keys exactly like the tmp/core reference.

#include "scene/AnimationClip.hpp"
#include "scene/AssetManager.hpp"
#include "scene/ByteArray.hpp"
#include "scene/Filesystem.hpp"
#include "scene/Material.hpp"
#include "scene/Mesh.hpp"
#include "scene/Skeleton.hpp"
#include "scene/SkinnedMesh.hpp"
#include <algorithm>
#include <array>
#include <coregl/gl_log.hpp>
#include <cstring>

namespace
{

constexpr gl::u32 IQM_VERSION = 2;
constexpr gl::u32 IQM_POSITION = 0, IQM_TEXCOORD = 1, IQM_NORMAL = 2;
constexpr gl::u32 IQM_BLENDINDEXES = 4, IQM_BLENDWEIGHTS = 5;
constexpr gl::u32 IQM_UBYTE = 1, IQM_FLOAT = 7;

#pragma pack(push, 1)
struct IqmHeader
{
    char magic[16];
    gl::u32 version, filesize, flags;
    gl::u32 num_text, ofs_text, num_meshes, ofs_meshes;
    gl::u32 num_vertexarrays, num_vertexes, ofs_vertexarrays;
    gl::u32 num_triangles, ofs_triangles, ofs_adjacency;
    gl::u32 num_joints, ofs_joints, num_poses, ofs_poses;
    gl::u32 num_anims, ofs_anims, num_frames, num_framechannels, ofs_frames, ofs_bounds;
    gl::u32 num_comment, ofs_comment, num_extensions, ofs_extensions;
};
struct IqmMesh
{
    gl::u32 name, material, first_vertex, num_vertexes, first_triangle, num_triangles;
};
struct IqmTriangle
{
    gl::u32 vertex[3];
};
struct IqmJoint
{
    gl::u32 name;
    int parent;
    float translate[3], rotate[4], scale[3];
};
struct IqmPose
{
    int parent;
    gl::u32 channelmask;
    float channeloffset[10], channelscale[10];
};
struct IqmAnim
{
    gl::u32 name, first_frame, num_frames;
    float framerate;
    gl::u32 flags;
};
struct IqmVertexArray
{
    gl::u32 type, flags, format, size, offset;
};
#pragma pack(pop)

std::string textAt(const std::vector<char>& t, gl::u32 o)
{
    return (o >= t.size()) ? std::string() : std::string(&t[o]);
}

template <typename T>
bool readArray(scene::ByteArray& b, gl::u32 fsz, gl::u32 ofs, gl::u32 count, std::vector<T>& out)
{
    out.clear();
    if (count == 0) return true;
    if (ofs == 0) return false;
    gl::u64 bytes = (gl::u64)count * sizeof(T);
    if ((gl::u64)ofs + bytes > (gl::u64)fsz) return false;
    out.resize(count);
    if (!b.seek(ofs)) return false;
    return b.readBytes(reinterpret_cast<gl::u8*>(out.data()), (gl::u32)bytes);
}

// decodes joint i's global bind matrix recursively (memoized via `state`),
// same scheme as tmp/core's globalBindRec
Mat4 globalBindRec(int i, const std::vector<IqmJoint>& joints, const std::vector<Mat4>& local,
                   std::vector<Mat4>& global, std::vector<gl::u8>& state)
{
    int n = (int)joints.size();
    if (i < 0 || i >= n) return Mat4::Identity();
    if (state[(size_t)i] == 2) return global[(size_t)i];
    state[(size_t)i] = 1;
    int parent = joints[(size_t)i].parent;
    global[(size_t)i] =
        (parent >= 0 && parent < n) ? globalBindRec(parent, joints, local, global, state) * local[(size_t)i]
                                    : local[(size_t)i];
    state[(size_t)i] = 2;
    return global[(size_t)i];
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

// samples pose i at `frame` into position/rotation/scale, same layout as
// tmp/core's decodeFrame (channelmask picks which of the 10 channels this
// frame overrides vs. leaves at channeloffset)
void decodeFrame(gl::u32 frame, gl::u32 numChannels, const std::vector<IqmPose>& poses,
                 const std::vector<gl::u16>& frameData, std::vector<Vec3>& oP, std::vector<Quaternion>& oR,
                 std::vector<Vec3>& oS)
{
    size_t pc = poses.size();
    oP.assign(pc, Vec3(0, 0, 0));
    oR.assign(pc, Quaternion(0, 0, 0, 1));
    oS.assign(pc, Vec3(1, 1, 1));
    if (!pc || frameData.empty() || !numChannels) return;
    gl::u64 base = (gl::u64)frame * numChannels;
    gl::u32 p = 0;
    for (size_t i = 0; i < pc; ++i)
    {
        const IqmPose& pose = poses[i];
        float ch[10];
        for (int c = 0; c < 10; ++c) ch[c] = pose.channeloffset[c];
        for (int c = 0; c < 10; ++c)
        {
            if ((pose.channelmask & (1u << c)) == 0u) continue;
            gl::u64 idx = base + p;
            if (idx < frameData.size()) ch[c] += (float)frameData[idx] * pose.channelscale[c];
            ++p;
        }
        oP[i] = Vec3(ch[0], ch[1], ch[2]);
        Quaternion q(ch[3], ch[4], ch[5], ch[6]);
        q.normalize();
        oR[i] = q;
        oS[i] = Vec3(ch[7], ch[8], ch[9]);
    }
}

// full file parse shared by load_iqm()/load_animations_iqm(): joints ->
// an already inverse-bound Skeleton, geometry (if any — anim-only IQM
// files have none), and the raw anim/pose/frame data needed to build clips.
struct IqmParsed
{
    bool ok = false;
    Skeleton skel;
    std::vector<MeshVertex> verts;
    std::vector<u32> indices;
    std::vector<SkinnedMesh::VertexWeights> weights;
    std::vector<IqmMesh> meshes;
    std::vector<char> text;
    std::vector<IqmPose> poses;
    std::vector<IqmAnim> iqmAnims;
    std::vector<gl::u16> frameData;
    gl::u32 numFrames = 0, numFrameChannels = 0;
};

IqmParsed parseIQM(const char* path)
{
    IqmParsed r;

    scene::ByteArray data;
    if (!fs::getFilesystem().readFile(path, data))
    {
        gl::Log::Error("[IQM] cannot read '%s'", path);
        return r;
    }
    gl::u32 fsz = data.size();
    if (fsz < sizeof(IqmHeader))
    {
        gl::Log::Error("[IQM] file too small: '%s'", path);
        return r;
    }

    IqmHeader h{};
    data.resetCursor();
    if (!data.readBytes(reinterpret_cast<gl::u8*>(&h), sizeof(h)))
    {
        gl::Log::Error("[IQM] truncated header: '%s'", path);
        return r;
    }
    if (std::memcmp(h.magic, "INTERQUAKEMODEL", 15) != 0 || h.version != IQM_VERSION)
    {
        gl::Log::Error("[IQM] bad magic/version: '%s'", path);
        return r;
    }

    if (h.num_text > 0)
    {
        r.text.resize(h.num_text);
        if (!data.seek(h.ofs_text) || !data.readBytes(reinterpret_cast<gl::u8*>(r.text.data()), h.num_text))
        {
            gl::Log::Error("[IQM] corrupt text section: '%s'", path);
            return r;
        }
    }

    std::vector<IqmVertexArray> varrays;
    std::vector<IqmTriangle> tris;
    std::vector<IqmJoint> joints;
    if (!readArray(data, fsz, h.ofs_meshes, h.num_meshes, r.meshes) ||
        !readArray(data, fsz, h.ofs_vertexarrays, h.num_vertexarrays, varrays) ||
        !readArray(data, fsz, h.ofs_triangles, h.num_triangles, tris))
    {
        gl::Log::Error("[IQM] corrupt mesh/vertex/triangle chunks: '%s'", path);
        return r;
    }
    readArray(data, fsz, h.ofs_joints, h.num_joints, joints);
    readArray(data, fsz, h.ofs_poses, h.num_poses, r.poses);
    readArray(data, fsz, h.ofs_anims, h.num_anims, r.iqmAnims);

    gl::u64 fcount = (gl::u64)h.num_frames * h.num_framechannels;
    if (fcount > 0)
    {
        r.frameData.resize((size_t)fcount);
        if (!data.seek(h.ofs_frames) ||
            !data.readBytes(reinterpret_cast<gl::u8*>(r.frameData.data()), (gl::u32)(fcount * 2)))
        {
            gl::Log::Error("[IQM] corrupt frames section: '%s'", path);
            return r;
        }
    }
    r.numFrames = h.num_frames;
    r.numFrameChannels = h.num_framechannels;

    // ── skeleton: bind TRS + inverse_bind (= inverse of the global bind) ──
    int n = (int)joints.size();
    std::vector<Mat4> local((size_t)n), global((size_t)n);
    std::vector<gl::u8> state((size_t)n, 0);
    for (int i = 0; i < n; ++i)
    {
        const IqmJoint& j = joints[(size_t)i];
        Vec3 t(j.translate[0], j.translate[1], j.translate[2]);
        Quaternion q(j.rotate[0], j.rotate[1], j.rotate[2], j.rotate[3]);
        q.normalize();
        Vec3 sc(j.scale[0], j.scale[1], j.scale[2]);
        local[(size_t)i] = Mat4::Translate(t) * Mat4(q) * Mat4::Scale(sc.x, sc.y, sc.z);
    }
    for (int i = 0; i < n; ++i) globalBindRec(i, joints, local, global, state);
    for (int i = 0; i < n; ++i)
    {
        const IqmJoint& j = joints[(size_t)i];
        std::string name = textAt(r.text, j.name);
        r.skel.add_bone(name.empty() ? ("joint_" + std::to_string(i)).c_str() : name.c_str(), j.parent,
                        local[(size_t)i], global[(size_t)i].inverted());
    }
    r.skel.finalize();

    // ── vertex arrays (geometry may be absent — anim-only IQM files) ──
    size_t vc = h.num_vertexes;
    if (vc > 0 && h.num_triangles > 0 && !r.meshes.empty())
    {
        std::vector<Vec3> pos(vc, Vec3(0, 0, 0)), nrm(vc, Vec3(0, 1, 0));
        std::vector<Vec2> uv(vc, Vec2(0, 0));
        std::vector<std::array<gl::u8, 4>> bids(vc, {{0, 0, 0, 0}});
        std::vector<Vec4> bw(vc, Vec4(1, 0, 0, 0));
        for (const IqmVertexArray& va : varrays)
        {
            if (va.offset == 0) continue;
            data.seek(va.offset);
            if (va.type == IQM_POSITION && va.format == IQM_FLOAT && va.size == 3)
                for (size_t i = 0; i < vc; ++i)
                {
                    float f0, f1, f2;
                    data.readF32(f0);
                    data.readF32(f1);
                    data.readF32(f2);
                    pos[i] = Vec3(f0, f1, f2);
                }
            else if (va.type == IQM_NORMAL && va.format == IQM_FLOAT && va.size == 3)
                for (size_t i = 0; i < vc; ++i)
                {
                    float f0, f1, f2;
                    data.readF32(f0);
                    data.readF32(f1);
                    data.readF32(f2);
                    nrm[i] = Vec3(f0, f1, f2);
                }
            else if (va.type == IQM_TEXCOORD && va.format == IQM_FLOAT && va.size == 2)
                for (size_t i = 0; i < vc; ++i)
                {
                    float f0, f1;
                    data.readF32(f0);
                    data.readF32(f1);
                    uv[i] = Vec2(f0, f1);
                }
            else if (va.type == IQM_BLENDINDEXES && va.format == IQM_UBYTE && va.size == 4)
                for (size_t i = 0; i < vc; ++i)
                    for (int k = 0; k < 4; ++k) data.readU8(bids[i][(size_t)k]);
            else if (va.type == IQM_BLENDWEIGHTS && va.format == IQM_UBYTE && va.size == 4)
                for (size_t i = 0; i < vc; ++i)
                {
                    gl::u8 w0 = 0, w1 = 0, w2 = 0, w3 = 0;
                    data.readU8(w0);
                    data.readU8(w1);
                    data.readU8(w2);
                    data.readU8(w3);
                    float sm = (float)w0 + w1 + w2 + w3;
                    bw[i] = (sm > 0) ? Vec4((float)w0 / sm, (float)w1 / sm, (float)w2 / sm, (float)w3 / sm)
                                     : Vec4(1, 0, 0, 0);
                }
            else if (va.type == IQM_BLENDWEIGHTS && va.format == IQM_FLOAT && va.size == 4)
                for (size_t i = 0; i < vc; ++i)
                {
                    float f0, f1, f2, f3;
                    data.readF32(f0);
                    data.readF32(f1);
                    data.readF32(f2);
                    data.readF32(f3);
                    bw[i] = Vec4(f0, f1, f2, f3);
                }
        }

        r.verts.resize(vc);
        r.weights.resize(vc);
        for (size_t i = 0; i < vc; ++i)
        {
            MeshVertex& v = r.verts[i];
            v.position = pos[i];
            v.normal = nrm[i].normalized();
            v.uv = uv[i];
            v.tangent = Vec4(1, 0, 0, 1);

            SkinnedMesh::VertexWeights& w = r.weights[i];
            for (int k = 0; k < 4; ++k) w.bone[k] = bids[i][(size_t)k];
            float sm = bw[i].x + bw[i].y + bw[i].z + bw[i].w;
            if (sm > 1e-6f)
            {
                w.weight[0] = bw[i].x / sm;
                w.weight[1] = bw[i].y / sm;
                w.weight[2] = bw[i].z / sm;
                w.weight[3] = bw[i].w / sm;
            }
            else
            {
                w.weight[0] = 1.f;
                w.weight[1] = w.weight[2] = w.weight[3] = 0.f;
            }
        }

        r.indices.reserve(tris.size() * 3);
        // winding flip to match the engine's front-face convention (same as MD3Loader)
        for (const IqmTriangle& tr : tris)
        {
            r.indices.push_back(tr.vertex[2]);
            r.indices.push_back(tr.vertex[1]);
            r.indices.push_back(tr.vertex[0]);
        }
    }

    r.ok = true;
    return r;
}

} // namespace

bool SkinnedMesh::load_iqm(const char* path)
{
    IqmParsed r = parseIQM(path);
    if (!r.ok) return false;
    if (r.verts.empty() || r.indices.empty() || r.meshes.empty())
    {
        gl::Log::Error("[IQM] '%s' has no geometry", path);
        return false;
    }

    m_skeleton = std::move(r.skel);
    m_weights = std::move(r.weights);

    std::string dir = dirOf(path);
    m_mesh.set_data(r.verts.data(), (u32)r.verts.size(), r.indices.data(), (u32)r.indices.size());
    for (size_t i = 0; i < r.meshes.size(); ++i)
    {
        const IqmMesh& m = r.meshes[i];
        std::string matName = textAt(r.text, m.material);
        Material* mat = new Material();
        if (!matName.empty()) mat->diffuse = tryLoadTex(dir, matName);
        m_materials.push_back(mat);

        u32 start = m.first_triangle * 3u, count = m.num_triangles * 3u;
        if (count > 0) m_mesh.add_surface(start, count, (int)i);
    }
    m_mesh.compute_tangents();

    // one clip per IqmAnim, decoded frame-by-frame into per-bone keys
    size_t boneCount = std::min((size_t)m_skeleton.bone_count(), r.poses.size());
    std::vector<Vec3> fp, fs;
    std::vector<Quaternion> fr;
    for (size_t ai = 0; ai < r.iqmAnims.size() && boneCount > 0; ++ai)
    {
        const IqmAnim& a = r.iqmAnims[ai];
        if (a.num_frames == 0) continue;

        AnimationClip* clip = new AnimationClip();
        std::string name = textAt(r.text, a.name);
        clip->set_name(name.empty() ? ("anim_" + std::to_string(ai)) : name);
        float fps = (a.framerate > 0.0f) ? a.framerate : 24.0f;
        clip->set_duration((a.num_frames > 1) ? (float)(a.num_frames - 1) / fps : 1.0f / fps);

        std::vector<BoneTrack> tracks(boneCount);
        for (size_t b = 0; b < boneCount; ++b) tracks[b].bone = (gl::i32)b;

        for (gl::u32 fi = 0; fi < a.num_frames; ++fi)
        {
            gl::u32 src = a.first_frame + fi;
            if (src >= r.numFrames) src = r.numFrames - 1;
            decodeFrame(src, r.numFrameChannels, r.poses, r.frameData, fp, fr, fs);
            float t = (float)fi / fps;
            for (size_t b = 0; b < boneCount; ++b)
            {
                tracks[b].times.push_back(t);
                tracks[b].positions.push_back(fp[b]);
                tracks[b].rotations.push_back(fr[b]);
                tracks[b].scales.push_back(fs[b]);
            }
        }
        clip->tracks() = std::move(tracks);
        m_clips.push_back(clip);
    }

    gl::Log::Info("[IQM] '%s': verts=%u tris=%u bones=%d surfs=%zu clips=%zu", path, (u32)r.verts.size(),
                  (u32)r.indices.size() / 3u, m_skeleton.bone_count(), r.meshes.size(), m_clips.size());
    m_loaded = true;
    return true;
}

// Anim-only .iqm (a separate export sharing the same joint names, e.g.
// idle.iqm/walk.iqm for one character): re-parses the file's joints/poses/
// anims (no geometry required) and rebinds tracks to the ALREADY-loaded
// skeleton by joint name — mirrors SkinnedMesh::load_animations_gltf's
// contract exactly.
bool SkinnedMesh::load_animations_iqm(const char* path)
{
    if (m_skeleton.empty())
    {
        gl::Log::Error("[IQM] load_animations_iqm('%s') called before a skeleton is loaded", path);
        return false;
    }

    IqmParsed r = parseIQM(path);
    if (!r.ok || r.skel.empty() || r.iqmAnims.empty())
    {
        gl::Log::Error("[IQM] '%s' has no usable animation", path);
        return false;
    }

    size_t boneCount = std::min((size_t)r.skel.bone_count(), r.poses.size());
    std::vector<int> remap(boneCount, -1);
    for (size_t b = 0; b < boneCount; ++b) remap[b] = m_skeleton.find_bone(r.skel.bone((int)b).name.c_str());

    std::vector<Vec3> fp, fs;
    std::vector<Quaternion> fr;
    int loaded = 0;
    for (size_t ai = 0; ai < r.iqmAnims.size() && boneCount > 0; ++ai)
    {
        const IqmAnim& a = r.iqmAnims[ai];
        if (a.num_frames == 0) continue;

        AnimationClip* clip = new AnimationClip();
        std::string name = textAt(r.text, a.name);
        clip->set_name(name.empty() ? ("anim_" + std::to_string(ai)) : name);
        float fps = (a.framerate > 0.0f) ? a.framerate : 24.0f;
        clip->set_duration((a.num_frames > 1) ? (float)(a.num_frames - 1) / fps : 1.0f / fps);

        std::vector<BoneTrack> tracks;
        tracks.reserve(boneCount);
        for (size_t b = 0; b < boneCount; ++b)
            if (remap[b] >= 0)
            {
                BoneTrack tr;
                tr.bone = remap[b];
                tracks.push_back(std::move(tr));
            }

        for (gl::u32 fi = 0; fi < a.num_frames; ++fi)
        {
            gl::u32 src = a.first_frame + fi;
            if (src >= r.numFrames) src = r.numFrames - 1;
            decodeFrame(src, r.numFrameChannels, r.poses, r.frameData, fp, fr, fs);
            float t = (float)fi / fps;
            size_t ti = 0;
            for (size_t b = 0; b < boneCount; ++b)
            {
                if (remap[b] < 0) continue;
                BoneTrack& tr = tracks[ti++];
                tr.times.push_back(t);
                tr.positions.push_back(fp[b]);
                tr.rotations.push_back(fr[b]);
                tr.scales.push_back(fs[b]);
            }
        }

        if (tracks.empty())
        {
            delete clip;
            continue;
        }
        clip->tracks() = std::move(tracks);
        gl::Log::Info("[IQM] clip '%s' (from '%s') — %.2fs, %zu tracks", clip->name().c_str(), path,
                      clip->duration(), clip->tracks().size());
        m_clips.push_back(clip);
        ++loaded;
    }

    if (loaded == 0)
    {
        gl::Log::Error("[IQM] '%s' — no clip's joints matched the loaded skeleton", path);
        return false;
    }
    return true;
}
