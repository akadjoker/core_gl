#include "scene/MeshLoader.hpp"
#include "scene/AssetManager.hpp"
#include "scene/ByteArray.hpp"
#include "scene/Filesystem.hpp"
#include "scene/Material.hpp"
#include "scene/Mesh.hpp"
#include <coregl/gl_log.hpp>
#include <algorithm>

// chunk ids — must match exporter/src/MeshFormat.hpp
static const u32 kMeshMagic = 0x4D455348; // "MESH"
static const u32 kChunkMats = 0x4D415453; // "MATS"
static const u32 kChunkBuff = 0x42554646; // "BUFF"
static const u32 kChunkVrts = 0x56525453; // "VRTS"
static const u32 kChunkIdxs = 0x49445853; // "IDXS"
static const u32 kChunkSkin = 0x534B494E; // "SKIN"
static const u32 kChunkSkel = 0x534B454C; // "SKEL"

// the file stores null-terminated strings (ByteArray::readString is
// length-prefixed, so read byte by byte here)
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

static bool parseMaterials(scene::ByteArray& in, std::vector<MeshLoader::MaterialDesc>& out)
{
    u32 count = 0;
    if (!in.readU32(count)) return false;
    for (u32 i = 0; i < count; ++i)
    {
        MeshLoader::MaterialDesc mat;
        if (!readCString(in, mat.name)) return false;
        if (!readVec3(in, mat.diffuse)) return false;
        if (!readVec3(in, mat.specular)) return false;
        if (!in.readF32(mat.shininess)) return false;
        gl::u8 texCount = 0;
        if (!in.readU8(texCount)) return false;
        for (gl::u8 t = 0; t < texCount; ++t)
        {
            std::string tex;
            if (!readCString(in, tex)) return false;
            mat.textures.push_back(tex);
        }
        out.push_back(mat);
    }
    return true;
}

static std::string dirOf(const std::string& path)
{
    auto slash = path.find_last_of('/');
    return slash == std::string::npos ? std::string() : path.substr(0, slash + 1);
}

// turns the parsed MaterialDesc list into real, ready-to-render Material*
// with textures loaded — so `instance->set_mesh(mesh)` alone is already
// correctly textured, no per-app material-building loop needed (mirrors
// SkinnedMesh::load()'s own buildMaterials).
static void buildMaterials(const std::string& path, const std::vector<MeshLoader::MaterialDesc>& descs,
                           Mesh& out)
{
    const std::string dir = dirOf(path);
    std::vector<Material*> mats;
    mats.reserve(descs.size());
    for (const MeshLoader::MaterialDesc& d : descs)
    {
        Material* mat = new Material(d.diffuse);
        mat->specular = d.specular.x;
        mat->shininess = d.shininess;
        if (!d.textures.empty())
        {
            std::string texPath = dir + d.textures[0];
            mat->diffuse = assets::AssetManager::instance().loadTexture(d.textures[0].c_str(),
                                                                        texPath.c_str());
        }
        mats.push_back(mat);
    }
    out.set_owned_materials(std::move(mats));
}

// one BUFF chunk = one surface, appended to the accumulated arrays
static bool parseBuffer(scene::ByteArray& in, u32 chunkEnd, std::vector<MeshVertex>& verts,
                        std::vector<u32>& indices, Mesh& mesh)
{
    u32 materialIndex = 0, flags = 0;
    if (!in.readU32(materialIndex) || !in.readU32(flags)) return false;

    const u32 baseVertex = (u32)verts.size();
    const u32 firstIndex = (u32)indices.size();
    Vec3 mn(1e30f, 1e30f, 1e30f), mx(-1e30f, -1e30f, -1e30f);

    while (in.cursor() + 8 <= chunkEnd)
    {
        u32 id = 0, length = 0;
        if (!in.readU32(id) || !in.readU32(length)) return false;
        const u32 subEnd = in.cursor() + length;

        if (id == kChunkVrts)
        {
            u32 n = 0;
            if (!in.readU32(n)) return false;

            // one bulk read for all n*8 floats (pos3+normal3+uv2), instead
            // of 8 separate ByteArray calls per vertex — the file layout
            // isn't the MeshVertex layout (no tangent, packed differently),
            // so we unpack after, but the read itself is a single memcpy
            std::vector<float> raw((size_t)n * 8);
            if (!in.readF32Array(raw.data(), n * 8)) return false;

            const size_t base = verts.size();
            verts.resize(base + n);
            for (u32 i = 0; i < n; ++i)
            {
                const float* r = &raw[(size_t)i * 8];
                MeshVertex& v = verts[base + i];
                v.position = Vec3(r[0], r[1], r[2]);
                v.normal = Vec3(r[3], r[4], r[5]);
                v.tangent = Vec4(1.f, 0.f, 0.f, 1.f); // recomputed after load
                v.uv = Vec2(r[6], r[7]);
                mn = mn.Min(v.position);
                mx = mx.Max(v.position);
            }
        }
        else if (id == kChunkIdxs)
        {
            u32 n = 0;
            if (!in.readU32(n)) return false;

            const size_t base = indices.size();
            indices.resize(base + n);
            if (!in.readU32Array(indices.data() + base, n)) return false;
            if (baseVertex != 0)
                for (u32 i = 0; i < n; ++i)
                    indices[base + i] += baseVertex;
        }
        else
        {
            // SKIN and anything newer: skip (static loading for now)
            in.setCursor(subEnd);
        }
        if (in.cursor() != subEnd) in.setCursor(subEnd);
    }

    mesh.add_surface(firstIndex, (u32)indices.size() - firstIndex, (int)materialIndex,
                     BoundingBox(mn, mx));
    return true;
}

bool MeshLoader::load(const char* path, Mesh& out, std::vector<MaterialDesc>* materials)
{
    scene::ByteArray data;
    if (!fs::getFilesystem().readFile(path, data))
    {
        gl::Log::Error("MeshLoader: cannot read '%s'", path);
        return false;
    }
    data.resetCursor();

    u32 magic = 0, version = 0;
    if (!data.readU32(magic) || !data.readU32(version) || magic != kMeshMagic)
    {
        gl::Log::Error("MeshLoader: '%s' is not a mesh file", path);
        return false;
    }

    std::vector<MeshVertex> verts;
    std::vector<u32> indices;
    std::vector<MaterialDesc> descs; // always captured, regardless of `materials`
    int buffers = 0;

    while (data.cursor() + 8 <= data.size())
    {
        u32 id = 0, length = 0;
        if (!data.readU32(id) || !data.readU32(length)) break;
        const u32 chunkEnd = data.cursor() + length;

        bool ok = true;
        if (id == kChunkMats)
            ok = parseMaterials(data, descs);
        else if (id == kChunkBuff)
        {
            ok = parseBuffer(data, chunkEnd, verts, indices, out);
            ++buffers;
        }
        else if (id == kChunkSkel)
        {
            // skeleton: skipped until skinned meshes land
        }

        if (!ok)
        {
            gl::Log::Error("MeshLoader: '%s' has a corrupt chunk (0x%08x)", path, id);
            return false;
        }
        data.setCursor(chunkEnd);
    }

    if (verts.empty() || indices.empty())
    {
        gl::Log::Error("MeshLoader: '%s' has no geometry", path);
        return false;
    }

    // set_data clears surfaces, so stash the ones parseBuffer registered
    std::vector<Surface> surfaces = out.surfaces();
    // Sort by material slot so the renderer naturally groups same-material
    // draws together — reduces texture binds with zero per-frame cost.
    std::sort(surfaces.begin(), surfaces.end(), [](const Surface& a, const Surface& b) {
        return a.material_slot < b.material_slot;
    });
    out.set_data(verts.data(), (u32)verts.size(), indices.data(), (u32)indices.size());
    for (const Surface& s : surfaces)
        out.add_surface(s.first_index, s.index_count, s.material_slot, s.bounds);
    out.compute_tangents();

    if (!descs.empty()) buildMaterials(path, descs, out);
    if (materials) *materials = descs;

    gl::Log::Info("MeshLoader: '%s' — %d surfaces, %u verts, %u indices, %zu materials", path,
                  buffers, (u32)verts.size(), (u32)indices.size(), descs.size());
    return true;
}
