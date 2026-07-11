#include "scene/SkinnedMesh.hpp"
#include "scene/ByteArray.hpp"
#include "scene/Filesystem.hpp"
#include <coregl/gl_log.hpp>

// chunk ids — must match exporter/src/MeshFormat.hpp
static const gl::u32 kMeshMagic = 0x4D455348; // "MESH"
static const gl::u32 kChunkBuff = 0x42554646; // "BUFF"
static const gl::u32 kChunkVrts = 0x56525453; // "VRTS"
static const gl::u32 kChunkIdxs = 0x49445853; // "IDXS"
static const gl::u32 kChunkSkin = 0x534B494E; // "SKIN"
static const gl::u32 kChunkSkel = 0x534B454C; // "SKEL"
static const gl::u32 kAnimMagic = 0x414E494D; // "ANIM"
static const gl::u32 kAnimInfo = 0x494E464F;  // "INFO"
static const gl::u32 kAnimChan = 0x4348414E;  // "CHAN"

static bool readCString(scene::ByteArray& in, std::string& out)
{
    out.clear();
    gl::u8 c;
    while (in.readU8(c))
    {
        if (c == 0) return true;
        out.push_back((char)c);
    }
    return false;
}

static bool readVec3(scene::ByteArray& in, Vec3& v)
{
    return in.readF32(v.x) && in.readF32(v.y) && in.readF32(v.z);
}

static bool readMat4(scene::ByteArray& in, Mat4& m)
{
    for (int i = 0; i < 16; ++i)
        if (!in.readF32(m.x[i])) return false;
    return true;
}

// one BUFF chunk = one surface; skinned buffers carry a SKIN sub-chunk with
// 4 bone ids + 4 weights per vertex, appended in the same order as verts
static bool parseBuffer(scene::ByteArray& in, gl::u32 chunkEnd, std::vector<MeshVertex>& verts,
                        std::vector<gl::u32>& indices,
                        std::vector<SkinnedMesh::VertexWeights>& weights, Mesh& mesh)
{
    gl::u32 materialIndex = 0, flags = 0;
    if (!in.readU32(materialIndex) || !in.readU32(flags)) return false;
    (void)flags;

    const gl::u32 baseVertex = (gl::u32)verts.size();
    const gl::u32 firstIndex = (gl::u32)indices.size();
    Vec3 mn(1e30f, 1e30f, 1e30f), mx(-1e30f, -1e30f, -1e30f);

    while (in.cursor() + 8 <= chunkEnd)
    {
        gl::u32 id = 0, length = 0;
        if (!in.readU32(id) || !in.readU32(length)) return false;
        const gl::u32 subEnd = in.cursor() + length;

        if (id == kChunkVrts)
        {
            gl::u32 n = 0;
            if (!in.readU32(n)) return false;

            // one bulk read for all n*8 floats instead of 8 ByteArray calls
            // per vertex (see MeshLoader.cpp — same fix, same reason)
            std::vector<float> raw((size_t)n * 8);
            if (!in.readF32Array(raw.data(), n * 8)) return false;

            const size_t base = verts.size();
            verts.resize(base + n);
            for (gl::u32 i = 0; i < n; ++i)
            {
                const float* r = &raw[(size_t)i * 8];
                MeshVertex& v = verts[base + i];
                v.position = Vec3(r[0], r[1], r[2]);
                v.normal = Vec3(r[3], r[4], r[5]);
                v.tangent = Vec4(1.f, 0.f, 0.f, 1.f);
                v.uv = Vec2(r[6], r[7]);
                mn = mn.Min(v.position);
                mx = mx.Max(v.position);
            }
        }
        else if (id == kChunkIdxs)
        {
            gl::u32 n = 0;
            if (!in.readU32(n)) return false;

            const size_t base = indices.size();
            indices.resize(base + n);
            if (!in.readU32Array(indices.data() + base, n)) return false;
            if (baseVertex != 0)
                for (gl::u32 i = 0; i < n; ++i)
                    indices[base + i] += baseVertex;
        }
        else if (id == kChunkSkin)
        {
            gl::u32 n = 0;
            if (!in.readU32(n)) return false;

            // VertexWeights (4x u8 + 4x f32 = 20 bytes) matches the file
            // layout exactly with no padding — one bulk read for the lot
            static_assert(sizeof(SkinnedMesh::VertexWeights) == 20,
                         "VertexWeights must match the file's packed layout");
            const size_t base = weights.size();
            weights.resize(base + n);
            if (!in.readBytes(reinterpret_cast<gl::u8*>(weights.data() + base), n * 20))
                return false;
        }
        else
            in.setCursor(subEnd);

        if (in.cursor() != subEnd) in.setCursor(subEnd);
    }

    mesh.add_surface(firstIndex, (gl::u32)indices.size() - firstIndex, (int)materialIndex,
                     BoundingBox(mn, mx));
    return true;
}

static bool parseSkeleton(scene::ByteArray& in, Skeleton& out)
{
    gl::u32 numBones = 0;
    if (!in.readU32(numBones)) return false;
    for (gl::u32 i = 0; i < numBones; ++i)
    {
        std::string name;
        gl::u32 parentBits = 0;
        Mat4 local, inverseBind;
        if (!readCString(in, name)) return false;
        if (!in.readU32(parentBits)) return false; // written as i32; -1 = root
        if (!readMat4(in, local) || !readMat4(in, inverseBind)) return false;
        out.add_bone(name.c_str(), (gl::i32)parentBits, local, inverseBind);
    }
    return true;
}

SkinnedMesh::~SkinnedMesh()
{
    for (AnimationClip* c : m_clips)
        delete c;
}

bool SkinnedMesh::load(const char* meshPath)
{
    scene::ByteArray data;
    if (!fs::getFilesystem().readFile(meshPath, data))
    {
        gl::Log::Error("SkinnedMesh: cannot read '%s'", meshPath);
        return false;
    }
    data.resetCursor();

    gl::u32 magic = 0, version = 0;
    if (!data.readU32(magic) || !data.readU32(version) || magic != kMeshMagic)
    {
        gl::Log::Error("SkinnedMesh: '%s' is not a mesh file", meshPath);
        return false;
    }

    std::vector<MeshVertex> verts;
    std::vector<gl::u32> indices;

    while (data.cursor() + 8 <= data.size())
    {
        gl::u32 id = 0, length = 0;
        if (!data.readU32(id) || !data.readU32(length)) break;
        const gl::u32 chunkEnd = data.cursor() + length;

        bool ok = true;
        if (id == kChunkBuff)
            ok = parseBuffer(data, chunkEnd, verts, indices, m_weights, m_mesh);
        else if (id == kChunkSkel)
        {
            ok = parseSkeleton(data, m_skeleton);
            m_skeleton.finalize(); // topological order for evaluate()
        }
        // MATS: names/textures — the game assigns its own materials

        if (!ok)
        {
            gl::Log::Error("SkinnedMesh: '%s' has a corrupt chunk (0x%08x)", meshPath, id);
            return false;
        }
        data.setCursor(chunkEnd);
    }

    if (verts.empty() || indices.empty() || m_skeleton.empty() ||
        m_weights.size() != verts.size())
    {
        gl::Log::Error("SkinnedMesh: '%s' — verts %u, weights %u, bones %d (need all, equal)",
                       meshPath, (gl::u32)verts.size(), (gl::u32)m_weights.size(),
                       m_skeleton.bone_count());
        return false;
    }

    std::vector<Surface> surfaces = m_mesh.surfaces();
    m_mesh.set_data(verts.data(), (gl::u32)verts.size(), indices.data(),
                    (gl::u32)indices.size());
    for (const Surface& s : surfaces)
        m_mesh.add_surface(s.first_index, s.index_count, s.material_slot, s.bounds);
    m_mesh.compute_tangents();

    gl::Log::Info("SkinnedMesh: '%s' — %u verts, %u indices, %d bones", meshPath,
                  (gl::u32)verts.size(), (gl::u32)indices.size(), m_skeleton.bone_count());
    m_loaded = true;
    return true;
}

// .anim: INFO (64-byte name, duration, ticksPerSecond, channels) then one
// CHAN per animated bone: cstring name, key count, keys of time + TRS.
// Times/durations are stored in source ticks; converted to seconds here.
bool SkinnedMesh::load_animations(const char* animPath)
{
    scene::ByteArray data;
    if (!fs::getFilesystem().readFile(animPath, data))
    {
        gl::Log::Error("SkinnedMesh: cannot read '%s'", animPath);
        return false;
    }
    data.resetCursor();

    gl::u32 magic = 0, version = 0;
    if (!data.readU32(magic) || !data.readU32(version) || magic != kAnimMagic)
    {
        gl::Log::Error("SkinnedMesh: '%s' is not an anim file", animPath);
        return false;
    }

    AnimationClip* clip = new AnimationClip();
    float ticksPerSecond = 0.f;
    int skippedTracks = 0;
    bool corrupt = false;

    while (!corrupt && data.cursor() + 8 <= data.size())
    {
        gl::u32 id = 0, length = 0;
        if (!data.readU32(id) || !data.readU32(length)) break;
        const gl::u32 chunkEnd = data.cursor() + length;

        if (id == kAnimInfo)
        {
            char name[64] = {};
            for (int i = 0; i < 64 && !corrupt; ++i)
                corrupt = !data.readU8(*(gl::u8*)&name[i]);
            name[63] = 0;
            float duration = 0.f;
            gl::u32 channels = 0;
            if (corrupt || !data.readF32(duration) || !data.readF32(ticksPerSecond) ||
                !data.readU32(channels))
            {
                corrupt = true;
                break;
            }
            clip->set_name(name);
            clip->set_duration(ticksPerSecond > 0.f ? duration / ticksPerSecond : duration);
        }
        else if (id == kAnimChan)
        {
            std::string boneName;
            gl::u32 numKeys = 0;
            if (!readCString(data, boneName) || !data.readU32(numKeys))
            {
                corrupt = true;
                break;
            }

            BoneTrack tr;
            tr.bone = m_skeleton.find_bone(boneName.c_str());
            tr.times.reserve(numKeys);
            tr.positions.reserve(numKeys);
            tr.rotations.reserve(numKeys);
            tr.scales.reserve(numKeys);
            const float invTps = ticksPerSecond > 0.f ? 1.f / ticksPerSecond : 1.f;
            for (gl::u32 k = 0; k < numKeys && !corrupt; ++k)
            {
                float t = 0.f;
                Vec3 pos, scl;
                Quaternion rot;
                corrupt = !data.readF32(t) || !readVec3(data, pos) || !data.readF32(rot.x) ||
                          !data.readF32(rot.y) || !data.readF32(rot.z) ||
                          !data.readF32(rot.w) || !readVec3(data, scl);
                if (corrupt) break;
                tr.times.push_back(t * invTps);
                tr.positions.push_back(pos);
                tr.rotations.push_back(rot);
                tr.scales.push_back(scl);
            }
            if (tr.bone >= 0)
                clip->tracks().push_back(std::move(tr));
            else
                ++skippedTracks; // channel for a node outside the skeleton
        }
        data.setCursor(chunkEnd);
    }

    if (corrupt || clip->tracks().empty())
    {
        gl::Log::Error("SkinnedMesh: '%s' is corrupt or has no usable tracks", animPath);
        delete clip;
        return false;
    }

    gl::Log::Info("SkinnedMesh: clip '%s' — %.2fs, %u tracks%s", clip->name().c_str(),
                  clip->duration(), (gl::u32)clip->tracks().size(),
                  skippedTracks ? " (some skipped)" : "");
    m_clips.push_back(clip);
    return true;
}

const AnimationClip* SkinnedMesh::find_clip(const std::string& name) const
{
    for (AnimationClip* c : m_clips)
        if (c && c->name() == name) return c;
    return nullptr;
}

// geometry + the extra skinning stream. Bone ids ride as floats (converted
// once here) so both attributes take the plain float path; the VAO assigns
// them the next locations after the mesh's own (4 = ids, 5 = weights).
bool SkinnedMesh::ensure_gpu()
{
    if (m_gpuReady) return true;
    if (!m_loaded) return false;

    m_mesh.upload();

    std::vector<float> stream;
    stream.reserve(m_weights.size() * 8);
    for (const VertexWeights& w : m_weights)
    {
        for (int j = 0; j < 4; ++j)
            stream.push_back((float)w.bone[j]);
        for (int j = 0; j < 4; ++j)
            stream.push_back(w.weight[j]);
    }
    m_weightsVbo.Allocate(gl::BufferType::ARRAY, stream.data(), stream.size() * sizeof(float),
                          gl::UsageType::STATIC_DRAW);

    const gl::VertexAttrib attribs[2] = {
        {gl::VertexAttribType::FLOAT, 4, 0, false},
        {gl::VertexAttribType::FLOAT, 4, 0, false},
    };
    m_mesh.vao().AddVertexBuffer(m_weightsVbo, attribs, 2, 8 * sizeof(float));

    m_gpuReady = true;
    return true;
}

void SkinnedMesh::release_gpu()
{
    m_mesh.release_gpu();
    m_weightsVbo.Release();
    m_gpuReady = false;
}
