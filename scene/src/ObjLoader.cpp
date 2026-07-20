// ObjLoader.cpp — plain Wavefront .obj (+ companion .mtl) mesh loader for
// the coregl scene layer. This is the hand-edit round-trip format: engine
// geometry (e.g. a converted BSP level) exports to .obj via
// tools/bspx_to_obj.py, gets fixed by hand in Blender, exports back out as
// .obj, and loads back in here — no engine-specific tooling needed for
// that loop, just whatever 3D editor already speaks OBJ.
//
// Text format, line-oriented, position/uv/normal each in their own index
// space (a face corner can reference different indices for each) — unlike
// every other loader here (MS3D/3DS/B3D/MD3), which are binary chunk
// streams read with ByteArray's cursor, so this one parses lines directly.

#include "scene/AssetManager.hpp"
#include "scene/ByteArray.hpp"
#include "scene/Filesystem.hpp"
#include "scene/Material.hpp"
#include "scene/Mesh.hpp"
#include <coregl/gl_log.hpp>
#include <array>
#include <cstdlib>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace
{

std::string obj_dirOf(const std::string& p)
{
    auto sl = p.find_last_of("/\\");
    return sl == std::string::npos ? std::string() : p.substr(0, sl + 1);
}

bool obj_readText(const std::string& path, std::string& out)
{
    scene::ByteArray bytes;
    if (!fs::getFilesystem().readText(path.c_str(), bytes)) return false;
    out.assign((const char*)bytes.data(), bytes.size());
    return true;
}

void obj_stripCR(std::string& line)
{
    if (!line.empty() && line.back() == '\r') line.pop_back();
}

struct ObjMaterial
{
    std::string name;
    Vec3 diffuse{1.f, 1.f, 1.f};
    Vec3 specular{0.f, 0.f, 0.f};
    float shininess = 32.f;
    std::string mapKd;
};

// parses one .mtl file referenced by mtllib, sitting next to the .obj
std::vector<ObjMaterial> obj_parseMtl(const std::string& dir, const std::string& mtlFile)
{
    std::vector<ObjMaterial> mats;
    std::string text;
    if (!obj_readText(dir + mtlFile, text))
    {
        gl::Log::Error("[OBJ] mtllib '%s' not found next to the .obj", mtlFile.c_str());
        return mats;
    }
    std::istringstream ss(text);
    std::string line;
    ObjMaterial* cur = nullptr;
    while (std::getline(ss, line))
    {
        obj_stripCR(line);
        std::istringstream ls(line);
        std::string tok;
        ls >> tok;
        if (tok == "newmtl")
        {
            std::string matName;
            ls >> matName;
            mats.push_back(ObjMaterial{});
            cur = &mats.back();
            cur->name = matName;
        }
        else if (tok == "Kd" && cur)
            ls >> cur->diffuse.x >> cur->diffuse.y >> cur->diffuse.z;
        else if (tok == "Ks" && cur)
            ls >> cur->specular.x >> cur->specular.y >> cur->specular.z;
        else if (tok == "Ns" && cur)
            ls >> cur->shininess;
        else if (tok == "map_Kd" && cur)
        {
            std::string rest;
            std::getline(ls, rest);
            size_t s = rest.find_first_not_of(' ');
            cur->mapKd = (s == std::string::npos) ? std::string() : rest.substr(s);
        }
    }
    return mats;
}

// one face-corner's index triple into positions/uvs/normals — each is its
// own 1-based index space (0 = absent, negative = relative-to-end per the
// OBJ spec, e.g. "-1" is the most recently declared v)
struct ObjFaceRef
{
    int v = 0, vt = 0, vn = 0;
};

ObjFaceRef obj_parseFaceRef(const std::string& tok, int vCount, int vtCount, int vnCount)
{
    int parts[3] = {0, 0, 0};
    int pi = 0;
    size_t start = 0;
    for (size_t i = 0; i <= tok.size() && pi < 3; ++i)
    {
        if (i == tok.size() || tok[i] == '/')
        {
            if (i > start) parts[pi] = atoi(tok.substr(start, i - start).c_str());
            ++pi;
            start = i + 1;
        }
    }
    auto resolve = [](int idx, int count) { return idx >= 0 ? idx : count + idx + 1; };
    ObjFaceRef r;
    r.v = resolve(parts[0], vCount);
    r.vt = resolve(parts[1], vtCount);
    r.vn = resolve(parts[2], vnCount);
    return r;
}

struct ObjGroup
{
    explicit ObjGroup(int s) : slot(s) {}
    int slot = 0;
    std::vector<std::array<ObjFaceRef, 3>> tris;
};

} // namespace

Mesh* assets::AssetManager::load_obj_mesh(const char* name, const char* path,
                                          std::vector<Material*>& out_mats,
                                          const char* textureDir)
{
    if (!name || !path) return nullptr;
    if (Mesh* existing = getMesh(name)) return existing;

    std::string text;
    if (!obj_readText(path, text))
    {
        gl::Log::Error("[OBJ] cannot read '%s'", path);
        return nullptr;
    }

    std::string dir = textureDir && *textureDir ? std::string(textureDir) : obj_dirOf(path);
    if (!dir.empty() && dir.back() != '/') dir += '/';

    std::vector<Vec3> positions;
    std::vector<Vec2> uvs;
    std::vector<Vec3> normals;

    std::vector<ObjMaterial> mtlMaterials;
    std::unordered_map<std::string, int> materialSlot; // usemtl name -> slot

    std::vector<ObjGroup> groups;
    int currentSlot = 0;

    auto currentGroup = [&]() -> ObjGroup& {
        if (groups.empty() || groups.back().slot != currentSlot)
            groups.push_back(ObjGroup(currentSlot));
        return groups.back();
    };

    std::istringstream ss(text);
    std::string line;
    while (std::getline(ss, line))
    {
        obj_stripCR(line);
        if (line.empty() || line[0] == '#') continue;
        std::istringstream ls(line);
        std::string tok;
        ls >> tok;

        if (tok == "v")
        {
            Vec3 p;
            ls >> p.x >> p.y >> p.z;
            positions.push_back(p);
        }
        else if (tok == "vt")
        {
            Vec2 uv;
            ls >> uv.x >> uv.y;
            uvs.push_back(uv);
        }
        else if (tok == "vn")
        {
            Vec3 n;
            ls >> n.x >> n.y >> n.z;
            normals.push_back(n);
        }
        else if (tok == "mtllib")
        {
            std::string mtlFile;
            ls >> mtlFile;
            for (ObjMaterial& m : obj_parseMtl(dir, mtlFile))
            {
                materialSlot[m.name] = (int)mtlMaterials.size();
                mtlMaterials.push_back(std::move(m));
            }
        }
        else if (tok == "usemtl")
        {
            std::string matName;
            ls >> matName;
            auto it = materialSlot.find(matName);
            if (it != materialSlot.end())
                currentSlot = it->second;
            else
            {
                // referenced but never declared by any mtllib seen so far —
                // register a plain default rather than dropping the faces
                currentSlot = (int)mtlMaterials.size();
                materialSlot[matName] = currentSlot;
                ObjMaterial m;
                m.name = matName;
                mtlMaterials.push_back(m);
            }
        }
        else if (tok == "f")
        {
            std::vector<ObjFaceRef> refs;
            std::string vtok;
            while (ls >> vtok)
                refs.push_back(
                    obj_parseFaceRef(vtok, (int)positions.size(), (int)uvs.size(), (int)normals.size()));
            if (refs.size() < 3) continue;
            ObjGroup& g = currentGroup();
            // fan triangulation — fine for the convex quads/ngons a level
            // export or simple prop produces; a genuinely concave ngon
            // would need ear-clipping, not worth it for hand-authored props
            for (size_t i = 1; i + 1 < refs.size(); ++i)
                g.tris.push_back({refs[0], refs[i], refs[i + 1]});
        }
        // g/o/s and anything else: not needed — surfaces are grouped by
        // material (usemtl) here, not by OBJ group/object name
    }

    if (positions.empty())
    {
        gl::Log::Error("[OBJ] '%s' has no geometry", path);
        return nullptr;
    }

    std::vector<MeshVertex> verts;
    std::vector<gl::u32> indices;
    const bool hasNormals = !normals.empty();

    struct SurfaceRange
    {
        gl::u32 firstIndex, count;
        int slot;
        Vec3 mn, mx;
    };
    std::vector<SurfaceRange> ranges;

    for (ObjGroup& g : groups)
    {
        gl::u32 firstIndex = (gl::u32)indices.size();
        Vec3 mn(1e30f, 1e30f, 1e30f), mx(-1e30f, -1e30f, -1e30f);
        for (auto& tri : g.tris)
        {
            for (int c = 0; c < 3; ++c)
            {
                const ObjFaceRef& r = tri[c];
                MeshVertex mv;
                if (r.v >= 1 && r.v <= (int)positions.size()) mv.position = positions[r.v - 1];
                if (r.vt >= 1 && r.vt <= (int)uvs.size()) mv.uv = uvs[r.vt - 1];
                if (r.vn >= 1 && r.vn <= (int)normals.size()) mv.normal = normals[r.vn - 1];
                verts.push_back(mv);
                indices.push_back((gl::u32)verts.size() - 1);
                mn = mn.Min(mv.position);
                mx = mx.Max(mv.position);
            }
        }
        ranges.push_back({firstIndex, (gl::u32)indices.size() - firstIndex, g.slot, mn, mx});
    }

    Mesh* mesh = createMesh(name);
    mesh->set_data(verts.data(), (gl::u32)verts.size(), indices.data(), (gl::u32)indices.size());
    for (const SurfaceRange& r : ranges)
    {
        if (r.count == 0) continue;
        mesh->add_surface(r.firstIndex, r.count, r.slot, BoundingBox(r.mn, r.mx));
    }
    if (!hasNormals) mesh->compute_normals();
    mesh->compute_tangents();
    mesh->upload();

    // owned by the Mesh, same contract every other loader here follows
    // (see load_bsp_mesh's doc comment in AssetManager.hpp) — out_mats is
    // filled from mesh->materials() below, a view, not a second owner.
    // Getting this wrong doesn't just leak: MeshInstance::set_mesh() reads
    // mesh->materials() to default its own (separate, snapshotted) list,
    // so materials that only ever lived in out_mats without being
    // registered on the Mesh are invisible to anything that mesh gets
    // attached to later, not just leaked.
    std::vector<Material*> builtMats;
    if (mtlMaterials.empty())
        builtMats.push_back(new Material());
    else
    {
        for (const ObjMaterial& m : mtlMaterials)
        {
            Material* mat = new Material();
            mat->base_color = m.diffuse;
            mat->specular = (m.specular.x + m.specular.y + m.specular.z) / 3.f;
            mat->shininess = m.shininess;
            if (!m.mapKd.empty())
            {
                std::string full = dir + m.mapKd;
                if (fs::getFilesystem().exists(full.c_str()))
                    mat->diffuse = loadTexture(m.mapKd.c_str(), full.c_str());
                else if (fs::getFilesystem().exists(m.mapKd.c_str()))
                    mat->diffuse = loadTexture(m.mapKd.c_str(), m.mapKd.c_str());
                else
                    gl::Log::Error("[OBJ] material texture '%s' not found (dir '%s')",
                                  m.mapKd.c_str(), dir.c_str());
            }
            builtMats.push_back(mat);
        }
    }
    mesh->set_owned_materials(builtMats);
    out_mats = mesh->materials();

    gl::Log::Info("[OBJ] '%s': verts=%zu tris=%zu materials=%zu", path, positions.size(),
                  indices.size() / 3, out_mats.size());
    return mesh;
}
