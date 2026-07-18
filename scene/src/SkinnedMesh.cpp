#include "scene/SkinnedMesh.hpp"
#include "scene/AssetManager.hpp"
#include "scene/ByteArray.hpp"
#include "scene/Filesystem.hpp"
#include "scene/Material.hpp"
#include <coregl/gl_log.hpp>
#include <algorithm>

// chunk ids — must match exporter/src/MeshFormat.hpp
static const gl::u32 kMeshMagic = 0x4D455348; // "MESH"
static const gl::u32 kChunkBuff = 0x42554646; // "BUFF"
static const gl::u32 kChunkVrts = 0x56525453; // "VRTS"
static const gl::u32 kChunkIdxs = 0x49445853; // "IDXS"
static const gl::u32 kChunkSkin = 0x534B494E; // "SKIN"
static const gl::u32 kChunkSkel = 0x534B454C; // "SKEL"
static const gl::u32 kChunkMats = 0x4D415453; // "MATS"
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

// MATS: numMaterials, then per material — cstring name, f32 diffuse xyz,
// f32 specular xyz, f32 shininess, u8 textureCount, then that many cstring
// texture filenames. Written by MeshWriter::WriteMaterialsChunk; index i
// here lines up 1:1 with Surface::material_slot == i.
static bool parseMaterials(scene::ByteArray& in, std::vector<SkinnedMesh::MaterialInfo>& out)
{
    gl::u32 numMats = 0;
    if (!in.readU32(numMats)) return false;
    out.resize(numMats);
     gl::Log::Info("SkinnedMesh: parsing %u materials", numMats);
    for (gl::u32 i = 0; i < numMats; ++i)
    {
        SkinnedMesh::MaterialInfo& m = out[i];
        if (!readCString(in, m.name)) return false;
        if (!readVec3(in, m.diffuse) || !readVec3(in, m.specular)) return false;
        if (!in.readF32(m.shininess)) return false;
        gl::u8 texCount = 0;
        if (!in.readU8(texCount)) return false;
        m.textures.resize(texCount);
        for (gl::u8 t = 0; t < texCount; ++t)
            if (!readCString(in, m.textures[t])) return false;
    }
    return true;
}

SkinnedMesh::~SkinnedMesh()
{
    for (AnimationClip* c : m_clips)
        delete c;
    for (Material* m : m_materials)
        delete m;
}

static std::string dirOf(const std::string& path)
{
    auto slash = path.find_last_of('/');
    return slash == std::string::npos ? std::string() : path.substr(0, slash + 1);
}

// turns material_infos() (raw MATS chunk data) into real, ready-to-render
// Material* with textures loaded — so a caller that just does
// `instance->set_mesh(res)` already gets a correctly multi-textured
// character, no per-app material-building loop required.
static void buildMaterials(const std::string& meshPath,
                           const std::vector<SkinnedMesh::MaterialInfo>& infos,
                           std::vector<Material*>& out)
{
    const std::string dir = dirOf(meshPath);
    out.reserve(infos.size());
    for (const SkinnedMesh::MaterialInfo& mi : infos)
    {
        Material* mat = new Material(mi.diffuse);
        mat->specular = mi.specular.x;
        mat->shininess = mi.shininess;
        if (!mi.textures.empty())
        {
            std::string texPath = dir + mi.textures[0];
            mat->diffuse = assets::AssetManager::instance().loadTexture(mi.textures[0].c_str(),
                                                                        texPath.c_str());
        }
        out.push_back(mat);
    }
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
        // glTF binary (.glb) starts with the ASCII magic "glTF"; JSON
        // (.gltf) starts with '{' (its first byte, since magic is the
        // first 4 bytes read as a little-endian u32) — either way, hand
        // off to the glTF path instead of failing.
        if (magic == 0x46546C67u || (magic & 0xFFu) == '{') return load_gltf(meshPath);
        // Blitz3D: root chunk tag "BB3D" read as the first 4 bytes.
        if (magic == 0x44334242u) return load_b3d(meshPath);
        // IQM: 16-byte "INTERQUAKEMODEL\0" magic — the first 4 bytes are "INTE".
        if (magic == 0x45544E49u) return load_iqm(meshPath);
        // MS3D: 10-byte ASCII "MS3D000000" magic — the first 4 bytes are
        // "MS3D", built from char literals (not a hand-computed LE hex
        // constant) to avoid a byte-order mistake.
        const gl::u32 kMS3DMagic =
            (gl::u32)'M' | ((gl::u32)'S' << 8) | ((gl::u32)'3' << 16) | ((gl::u32)'D' << 24);
        if (magic == kMS3DMagic) return load_ms3d(meshPath);
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
        else if (id == kChunkMats)
            ok = parseMaterials(data, m_materialInfos);

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
    // Sort by material slot — groups same-material draws together.
    std::sort(surfaces.begin(), surfaces.end(), [](const Surface& a, const Surface& b) {
        return a.material_slot < b.material_slot;
    });
    m_mesh.set_data(verts.data(), (gl::u32)verts.size(), indices.data(),
                    (gl::u32)indices.size());
    for (const Surface& s : surfaces)
        m_mesh.add_surface(s.first_index, s.index_count, s.material_slot, s.bounds);
    m_mesh.compute_tangents();

    if (!m_materialInfos.empty()) buildMaterials(meshPath, m_materialInfos, m_materials);

    gl::Log::Info("SkinnedMesh: '%s' — %u verts, %u indices, %d bones, %zu materials", meshPath,
                  (gl::u32)verts.size(), (gl::u32)indices.size(), m_skeleton.bone_count(),
                  m_materials.size());
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
        if (magic == 0x46546C67u || (magic & 0xFFu) == '{') return load_animations_gltf(animPath);
        if (magic == 0x44334242u) return load_animations_b3d(animPath);
        if (magic == 0x45544E49u) return load_animations_iqm(animPath);
        const gl::u32 kMS3DMagic =
            (gl::u32)'M' | ((gl::u32)'S' << 8) | ((gl::u32)'3' << 16) | ((gl::u32)'D' << 24);
        if (magic == kMS3DMagic) return load_animations_ms3d(animPath);
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

bool SkinnedMesh::set_material_texture(int slot, gl::Texture* tex)
{
    if (slot < 0 || slot >= (int)m_materials.size()) return false;
    Material* m = get_material(slot);
    if (!m) return false;
    m->diffuse = tex;
    return true;
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
