// C3DSLoader.cpp — Autodesk 3D Studio (.3ds) static mesh loader for the
// coregl scene layer. Ported from tmp/C3DSMeshFileLoader.{h,cpp} (Irrlicht's
// C3DSMeshFileLoader) onto scene::ByteArray/Mesh/Material/AssetManager —
// same pattern as MD2Loader.cpp/MD3Loader.cpp/BSPLoader.cpp: one static
// loader, no runtime Irrlicht/assimp dependency.
//
// Only what a static prop needs is kept: vertices, per-face indices, UVs,
// per-material face groups, material name/diffuse-color/texture-map. Key
// frame (0xB000) and hierarchy chunks are skipped — this loader treats
// every 3DS file as a bag of static triangle meshes, which is exactly what
// the Hoverjet Racing prop set (astro1..7.3ds) needs.
#include "scene/AssetManager.hpp"
#include "scene/ByteArray.hpp"
#include "scene/Filesystem.hpp"
#include "scene/Material.hpp"
#include "scene/Mesh.hpp"
#include <coregl/gl_log.hpp>
#include <cctype>
#include <cstdio>
#include <unordered_map>

namespace
{

enum e3DSChunk : gl::u16
{
    C3DS_MAIN3DS = 0x4D4D,

    C3DS_EDIT3DS = 0x3D3D,
    C3DS_KEYF3DS = 0xB000,
    C3DS_VERSION = 0x0002,
    C3DS_MESHVERSION = 0x3D3E,

    C3DS_EDIT_MATERIAL = 0xAFFF,
    C3DS_EDIT_OBJECT = 0x4000,

    C3DS_MATNAME = 0xA000,
    C3DS_MATDIFFUSE = 0xA020,
    C3DS_MATTEXMAP = 0xA200,
    C3DS_MATMAPFILE = 0xA300,

    C3DS_OBJTRIMESH = 0x4100,
    C3DS_TRIVERT = 0x4110,
    C3DS_POINTFLAGARRAY = 0x4111,
    C3DS_TRIFACE = 0x4120,
    C3DS_TRIFACEMAT = 0x4130,
    C3DS_TRIUV = 0x4140,
    C3DS_TRIMATRIX = 0x4160,
    C3DS_MESHCOLOR = 0x4165,
    C3DS_TRISMOOTH = 0x4150,

    C3DS_COL_RGB = 0x0010,
    C3DS_COL_TRU = 0x0011,
    C3DS_COL_LIN_24 = 0x0012,
    C3DS_COL_LIN_F = 0x0013,
};

struct Face3DS
{
    gl::u16 a, b, c;
};

struct FaceGroup3DS
{
    std::string matName;
    std::vector<gl::u16> faces; // indices into Object3DS::faces
};

struct Object3DS
{
    std::string name;
    std::vector<Vec3> verts;
    std::vector<Vec2> uvs;
    std::vector<Face3DS> faces;
    std::vector<FaceGroup3DS> groups;
};

struct Material3DS
{
    std::string name;
    Vec3 diffuse = Vec3(1.f, 1.f, 1.f);
    std::string texFile;
};

// 3DS Studio's *global* viewport is Z-up, but object-local vertex data is
// exported however the artist oriented the piece in it — there's no fixed
// per-file axis rule. Apocalyx's own native 3ds reader (tmp/apocalyx/
// dataread.cpp, DRMeshReader::readMeshes/FMT_3DS) confirms this: it copies
// (x,y,z) straight through with no swap, and these particular assets
// (astro*.3ds) were authored/exported for exactly that pipeline. Swapping
// here (like MD2/MD3/B3D's fixed Z-up->Y-up do) rotates them 90 deg off —
// verified by eye: the ship pointed at the sky until this was removed.
Vec3 rawVertex(float x, float y, float z) { return Vec3(x, y, z); }

bool readChunkHeader(scene::ByteArray& in, gl::u16& id, gl::u32& end)
{
    gl::u32 len = 0;
    if (!in.readU16(id) || !in.readU32(len)) return false;
    if (len < 6) return false;
    end = in.cursor() + (len - 6);
    return true;
}

bool readCString(scene::ByteArray& in, std::string& out)
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

// Reads a single COL_RGB/COL_TRU/... chunk. `end` bounds the whole chunk
// that's supposed to contain it (there's normally exactly one).
void readColorChunk(scene::ByteArray& in, gl::u32 end, Vec3& outColor)
{
    while (in.cursor() + 6 <= end)
    {
        gl::u16 id;
        gl::u32 subEnd;
        if (!readChunkHeader(in, id, subEnd)) return;
        if (id == C3DS_COL_RGB || id == C3DS_COL_LIN_F)
        {
            float r = 0, g = 0, b = 0;
            in.readF32(r);
            in.readF32(g);
            in.readF32(b);
            outColor = Vec3(r, g, b);
        }
        else if (id == C3DS_COL_TRU || id == C3DS_COL_LIN_24)
        {
            gl::u8 r = 0, g = 0, b = 0;
            in.readU8(r);
            in.readU8(g);
            in.readU8(b);
            outColor = Vec3(r / 255.f, g / 255.f, b / 255.f);
        }
        in.setCursor(subEnd);
        return; // exactly one color subchunk expected
    }
}

void readMaterialChunk(scene::ByteArray& in, gl::u32 matEnd, std::vector<Material3DS>& out)
{
    Material3DS mat;
    gl::u16 texSection = 0; // which map chunk we're currently inside (only TEXMAP handled)

    while (in.cursor() + 6 <= matEnd)
    {
        gl::u16 id;
        gl::u32 subEnd;
        if (!readChunkHeader(in, id, subEnd)) break;

        switch (id)
        {
        case C3DS_MATNAME:
            readCString(in, mat.name);
            in.setCursor(subEnd);
            break;
        case C3DS_MATDIFFUSE:
            readColorChunk(in, subEnd, mat.diffuse);
            in.setCursor(subEnd);
            break;
        // MATTEXMAP is a *container* (its declared length spans its
        // children too) — don't jump to subEnd here or MATMAPFILE inside
        // it never gets read. Just record which section we're in and keep
        // looping; the cursor naturally lands back on subEnd once every
        // child chunk below has consumed its own bytes.
        case C3DS_MATTEXMAP: texSection = id; break;
        case C3DS_MATMAPFILE:
            if (texSection == C3DS_MATTEXMAP) readCString(in, mat.texFile);
            in.setCursor(subEnd);
            break;
        default: in.setCursor(subEnd); break;
        }
    }
    out.push_back(mat);
}

void readTriVert(scene::ByteArray& in, Object3DS& obj)
{
    gl::u16 n = 0;
    in.readU16(n);
    obj.verts.resize(n);
    for (gl::u16 i = 0; i < n; ++i)
    {
        float x = 0, y = 0, z = 0;
        in.readF32(x);
        in.readF32(y);
        in.readF32(z);
        obj.verts[i] = rawVertex(x, y, z);
    }
}

void readTriUV(scene::ByteArray& in, Object3DS& obj)
{
    gl::u16 n = 0;
    in.readU16(n);
    obj.uvs.resize(n);
    for (gl::u16 i = 0; i < n; ++i)
    {
        float u = 0, v = 0;
        in.readF32(u);
        in.readF32(v);
        obj.uvs[i] = Vec2(u, 1.f - v);
    }
}

void readFaceGroup(scene::ByteArray& in, Object3DS& obj)
{
    FaceGroup3DS group;
    readCString(in, group.matName);
    gl::u16 n = 0;
    in.readU16(n);
    group.faces.resize(n);
    for (gl::u16 i = 0; i < n; ++i)
    {
        gl::u16 f = 0;
        in.readU16(f);
        group.faces[i] = f;
    }
    obj.groups.push_back(std::move(group));
}

// TRIFACE: u16 count, then count*(3 indices + 1 edge-flag) u16s, followed by
// nested chunks (TRIFACEMAT groups, smoothing groups) up to `end`.
void readTriFace(scene::ByteArray& in, gl::u32 end, Object3DS& obj)
{
    gl::u16 n = 0;
    in.readU16(n);
    obj.faces.resize(n);
    for (gl::u16 i = 0; i < n; ++i)
    {
        gl::u16 a = 0, b = 0, c = 0, flag = 0;
        in.readU16(a);
        in.readU16(b);
        in.readU16(c);
        in.readU16(flag);
        obj.faces[i] = {a, b, c};
    }

    while (in.cursor() + 6 <= end)
    {
        gl::u16 id;
        gl::u32 subEnd;
        if (!readChunkHeader(in, id, subEnd)) break;
        if (id == C3DS_TRIFACEMAT)
            readFaceGroup(in, obj);
        // TRISMOOTH / POINTFLAGARRAY / others: not needed for a static prop
        in.setCursor(subEnd);
    }
}

void readTriMesh(scene::ByteArray& in, gl::u32 end, Object3DS& obj)
{
    while (in.cursor() + 6 <= end)
    {
        gl::u16 id;
        gl::u32 subEnd;
        if (!readChunkHeader(in, id, subEnd)) break;

        switch (id)
        {
        case C3DS_TRIVERT: readTriVert(in, obj); break;
        case C3DS_TRIUV: readTriUV(in, obj); break;
        case C3DS_TRIFACE: readTriFace(in, subEnd, obj); break;
        // TRIMATRIX (local pivot transform) is parsed by the reference
        // loader but never actually applied to vertices either — the
        // faces come out in world space already for these prop meshes.
        default: break;
        }
        in.setCursor(subEnd);
    }
}

void readEditObject(scene::ByteArray& in, gl::u32 end, std::vector<Object3DS>& objects)
{
    Object3DS obj;
    readCString(in, obj.name);

    while (in.cursor() + 6 <= end)
    {
        gl::u16 id;
        gl::u32 subEnd;
        if (!readChunkHeader(in, id, subEnd)) break;
        if (id == C3DS_OBJTRIMESH) readTriMesh(in, subEnd, obj);
        in.setCursor(subEnd);
    }

    if (!obj.faces.empty()) objects.push_back(std::move(obj));
}

void readEdit3DS(scene::ByteArray& in, gl::u32 end, std::vector<Object3DS>& objects,
                 std::vector<Material3DS>& materials)
{
    while (in.cursor() + 6 <= end)
    {
        gl::u16 id;
        gl::u32 subEnd;
        if (!readChunkHeader(in, id, subEnd)) break;

        if (id == C3DS_EDIT_MATERIAL)
            readMaterialChunk(in, subEnd, materials);
        else if (id == C3DS_EDIT_OBJECT)
            readEditObject(in, subEnd, objects);
        in.setCursor(subEnd);
    }
}

std::string dirOf(const std::string& p)
{
    auto sl = p.find_last_of('/');
    return sl == std::string::npos ? std::string() : p.substr(0, sl + 1);
}

// 3DS material map filenames often carry the DOS-era case/extension the
// asset was authored with; try it verbatim, then lower-cased.
gl::Texture* tryLoadMap(const std::string& dir, const std::string& fileName)
{
    if (fileName.empty()) return nullptr;
    auto tryPath = [&](const std::string& name) -> gl::Texture* {
        std::string path = dir + name;
        if (!fs::getFilesystem().exists(path.c_str())) return nullptr;
        return assets::AssetManager::instance().loadTexture(name.c_str(), path.c_str());
    };
    if (gl::Texture* t = tryPath(fileName)) return t;
    std::string lower = fileName;
    for (char& ch : lower) ch = (char)tolower((unsigned char)ch);
    return tryPath(lower);
}

} // namespace

Mesh* assets::AssetManager::load_3ds_mesh(const char* name, const char* path,
                                          std::vector<Material*>& out_mats,
                                          const char* textureDir)
{
    if (!name || !path) return nullptr;
    if (Mesh* existing = getMesh(name)) return existing;

    scene::ByteArray data;
    if (!fs::getFilesystem().readFile(path, data))
    {
        gl::Log::Error("3DS: cannot read '%s'", path);
        return nullptr;
    }
    data.resetCursor();

    gl::u16 mainId;
    gl::u32 mainEnd;
    if (!readChunkHeader(data, mainId, mainEnd) || mainId != C3DS_MAIN3DS)
    {
        gl::Log::Error("3DS: '%s' is not a 3ds file", path);
        return nullptr;
    }

    std::vector<Object3DS> objects;
    std::vector<Material3DS> materials;

    while (data.cursor() + 6 <= mainEnd)
    {
        gl::u16 id;
        gl::u32 end;
        if (!readChunkHeader(data, id, end)) break;
        if (id == C3DS_EDIT3DS) readEdit3DS(data, end, objects, materials);
        data.setCursor(end);
    }

    if (objects.empty())
    {
        gl::Log::Error("3DS: '%s' has no geometry", path);
        return nullptr;
    }

    // material name -> slot in the output Material list (built once, shared
    // across every object/face-group that references it)
    std::unordered_map<std::string, int> matSlots;
    std::vector<Material*> mats;
    const std::string dir = textureDir && *textureDir ? std::string(textureDir) : dirOf(path);
    auto slotFor = [&](const std::string& matName) -> int {
        if (matName.empty()) return -1;
        auto it = matSlots.find(matName);
        if (it != matSlots.end()) return it->second;
        int slot = (int)mats.size();
        matSlots[matName] = slot;
        Material* mat = new Material();
        for (const Material3DS& m : materials)
            if (m.name == matName)
            {
                mat->base_color = m.diffuse;
                mat->diffuse = tryLoadMap(dir, m.texFile);
                break;
            }
        mats.push_back(mat);
        return slot;
    };
    // material with no name (objects with no TRIFACEMAT groups at all)
    int defaultSlot = -1;
    auto ensureDefaultSlot = [&]() -> int {
        if (defaultSlot < 0)
        {
            defaultSlot = (int)mats.size();
            mats.push_back(new Material());
        }
        return defaultSlot;
    };

    std::vector<MeshVertex> verts;
    std::vector<u32> indices;
    struct SurfDesc
    {
        u32 first, count;
        int slot;
        Vec3 mn, mx;
    };
    std::vector<SurfDesc> surfaceDescs;

    for (const Object3DS& obj : objects)
    {
        // group -> face list; ungrouped faces (no TRIFACEMAT at all) fall
        // back to one default-material group covering every face
        std::vector<const FaceGroup3DS*> groups;
        FaceGroup3DS fallback;
        if (obj.groups.empty())
        {
            fallback.matName.clear();
            fallback.faces.resize(obj.faces.size());
            for (gl::u16 i = 0; i < (gl::u16)obj.faces.size(); ++i) fallback.faces[i] = i;
            groups.push_back(&fallback);
        }
        else
        {
            for (const FaceGroup3DS& g : obj.groups) groups.push_back(&g);
        }

        for (const FaceGroup3DS* g : groups)
        {
            if (g->faces.empty()) continue;
            int slot = g->matName.empty() ? ensureDefaultSlot() : slotFor(g->matName);

            std::unordered_map<gl::u16, u32> remap; // object vertex idx -> global vertex idx
            u32 first = (u32)indices.size();
            Vec3 mn(1e30f, 1e30f, 1e30f), mx(-1e30f, -1e30f, -1e30f);

            for (gl::u16 faceIdx : g->faces)
            {
                if (faceIdx >= obj.faces.size()) continue;
                const Face3DS& f = obj.faces[faceIdx];
                gl::u16 fi[3] = {f.a, f.b, f.c};
                for (gl::u16 vi : fi)
                {
                    auto it = remap.find(vi);
                    u32 gi;
                    if (it != remap.end())
                        gi = it->second;
                    else
                    {
                        MeshVertex mv;
                        mv.position = vi < obj.verts.size() ? obj.verts[vi] : Vec3(0, 0, 0);
                        mv.normal = Vec3(0.f, 1.f, 0.f); // recomputed below
                        mv.tangent = Vec4(1.f, 0.f, 0.f, 1.f);
                        mv.uv = vi < obj.uvs.size() ? obj.uvs[vi] : Vec2(0, 0);
                        gi = (u32)verts.size();
                        verts.push_back(mv);
                        remap[vi] = gi;
                        mn = mn.Min(mv.position);
                        mx = mx.Max(mv.position);
                    }
                    indices.push_back(gi);
                }
            }

            surfaceDescs.push_back({first, (u32)indices.size() - first, slot, mn, mx});
        }
    }

    if (verts.empty() || indices.empty())
    {
        gl::Log::Error("3DS: '%s' produced no triangles", path);
        for (Material* m : mats) delete m;
        return nullptr;
    }

    Mesh* mesh = createMesh(name);
    mesh->set_data(verts.data(), (u32)verts.size(), indices.data(), (u32)indices.size());
    for (const SurfDesc& s : surfaceDescs)
        mesh->add_surface(s.first, s.count, s.slot, BoundingBox(s.mn, s.mx));
    mesh->compute_normals();
    mesh->compute_tangents();
    mesh->set_owned_materials(mats);
    mesh->upload();

    out_mats = mats;
    gl::Log::Info("3DS: '%s' — %d object(s), %u verts, %u tris, %zu materials", path,
                  (int)objects.size(), (u32)verts.size(), (u32)(indices.size() / 3), mats.size());
    return mesh;
}
