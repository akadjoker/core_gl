#include "OgreMeshLoader.hpp"
#include "Types.hpp"
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <vector>

namespace
{
// ── chunk ids (OgreMain/include/OgreMeshFileFormat.h, verified against the
// real Ogre 14.5.1 source and byte-checked against Columns.mesh) ──
constexpr u16 M_HEADER = 0x1000;
constexpr u16 M_MESH = 0x3000;
constexpr u16 M_SUBMESH = 0x4000;
constexpr u16 M_GEOMETRY = 0x5000;
constexpr u16 M_GEOMETRY_VERTEX_DECLARATION = 0x5100;
constexpr u16 M_GEOMETRY_VERTEX_ELEMENT = 0x5110;
constexpr u16 M_GEOMETRY_VERTEX_BUFFER = 0x5200;
constexpr u16 M_GEOMETRY_VERTEX_BUFFER_DATA = 0x5210;

// vertex element semantics we care about (VertexElementSemantic)
constexpr u16 VES_POSITION = 1;
constexpr u16 VES_NORMAL = 4;
constexpr u16 VES_TEXTURE_COORDINATES = 7;

// vertex element types we accept (VertexElementType) — anything else is a
// format assumption we don't support, so Load() fails loudly instead of
// misreading bytes
constexpr u16 VET_FLOAT1 = 0;
constexpr u16 VET_FLOAT2 = 1;
constexpr u16 VET_FLOAT3 = 2;

// ── flat byte-buffer reader ──
class ByteReader
{
public:
    explicit ByteReader(const std::vector<u8>& data) : m_data(data), m_pos(0) {}

    size_t Tell() const { return m_pos; }
    size_t Size() const { return m_data.size(); }

    bool CanRead(size_t n) const { return m_pos + n <= m_data.size(); }

    void Seek(size_t pos) { m_pos = pos; }

    u16 ReadU16()
    {
        u16 v = 0;
        ReadBytes(&v, 2);
        return v;
    }
    u32 ReadU32()
    {
        u32 v = 0;
        ReadBytes(&v, 4);
        return v;
    }
    bool ReadBool()
    {
        u8 v = 0;
        ReadBytes(&v, 1);
        return v != 0;
    }
    // '\n'-terminated (Serializer::readString == DataStream::getLine), no
    // length prefix, no null byte
    std::string ReadString()
    {
        std::string s;
        while (m_pos < m_data.size() && m_data[m_pos] != '\n')
            s.push_back((char)m_data[m_pos++]);
        if (m_pos < m_data.size()) ++m_pos; // consume the newline
        return s;
    }
    void ReadBytes(void* dst, size_t n)
    {
        if (!CanRead(n))
        {
            std::cerr << "OgreMeshLoader: unexpected end of file\n";
            m_pos = m_data.size();
            memset(dst, 0, n);
            return;
        }
        memcpy(dst, m_data.data() + m_pos, n);
        m_pos += n;
    }

private:
    const std::vector<u8>& m_data;
    size_t m_pos;
};

struct ChunkHeader
{
    u16 id;
    u32 length; // includes this 6-byte header
};

ChunkHeader ReadChunkHeader(ByteReader& r)
{
    ChunkHeader h;
    h.id = r.ReadU16();
    h.length = r.ReadU32();
    return h;
}

struct VertexElementDecl
{
    u16 source, type, semantic, offset, index;
};

struct VertexBufferBind
{
    u16 vertexSize;
    std::vector<u8> data;
};

// M_GEOMETRY: vertex count, a declaration (one element per attribute) and
// one or more raw interleaved vertex buffers, one per declared "source"
// (bind index). Extracts POSITION/NORMAL/TEXCOORD0 into `outVerts`.
bool ReadGeometry(ByteReader& r, size_t endPos, std::vector<Vertex>& outVerts, bool verbose)
{
    u32 vertexCount = r.ReadU32();

    std::vector<VertexElementDecl> decl;
    std::map<u16, VertexBufferBind> buffers;

    while (r.Tell() < endPos)
    {
        ChunkHeader ch = ReadChunkHeader(r);
        size_t childEnd = r.Tell() + ch.length - 6;

        if (ch.id == M_GEOMETRY_VERTEX_DECLARATION)
        {
            while (r.Tell() < childEnd)
            {
                ReadChunkHeader(r); // M_GEOMETRY_VERTEX_ELEMENT, fixed 10-byte payload
                VertexElementDecl e;
                e.source = r.ReadU16();
                e.type = r.ReadU16();
                e.semantic = r.ReadU16();
                e.offset = r.ReadU16();
                e.index = r.ReadU16();
                decl.push_back(e);
            }
        }
        else if (ch.id == M_GEOMETRY_VERTEX_BUFFER)
        {
            u16 bindIndex = r.ReadU16();
            u16 vertexSize = r.ReadU16();
            ChunkHeader dataHeader = ReadChunkHeader(r); // M_GEOMETRY_VERTEX_BUFFER_DATA
            size_t dataLen = dataHeader.length - 6;
            VertexBufferBind bind;
            bind.vertexSize = vertexSize;
            bind.data.resize(dataLen);
            r.ReadBytes(bind.data.data(), dataLen);
            buffers[bindIndex] = std::move(bind);
        }
        else
        {
            r.Seek(childEnd); // unrecognized at this level — skip
        }
    }

    const VertexElementDecl *posEl = nullptr, *normEl = nullptr, *uvEl = nullptr;
    for (const VertexElementDecl& e : decl)
    {
        if (e.semantic == VES_POSITION) posEl = &e;
        else if (e.semantic == VES_NORMAL) normEl = &e;
        else if (e.semantic == VES_TEXTURE_COORDINATES && e.index == 0) uvEl = &e;
    }
    if (!posEl)
    {
        std::cerr << "OgreMeshLoader: geometry has no POSITION element\n";
        return false;
    }
    if (posEl->type != VET_FLOAT3)
    {
        std::cerr << "OgreMeshLoader: POSITION element is not FLOAT3 (type=" << posEl->type
                  << ") — unsupported, refusing to misread\n";
        return false;
    }
    if (normEl && normEl->type != VET_FLOAT3)
    {
        std::cerr << "OgreMeshLoader: NORMAL element is not FLOAT3 (type=" << normEl->type
                  << ") — unsupported\n";
        return false;
    }
    if (uvEl && uvEl->type != VET_FLOAT2)
    {
        std::cerr << "OgreMeshLoader: TEXCOORD0 element is not FLOAT2 (type=" << uvEl->type
                  << ") — unsupported\n";
        return false;
    }

    auto findBuffer = [&](const VertexElementDecl* e) -> const VertexBufferBind* {
        if (!e) return nullptr;
        auto it = buffers.find(e->source);
        return it == buffers.end() ? nullptr : &it->second;
    };
    const VertexBufferBind* posBuf = findBuffer(posEl);
    if (!posBuf)
    {
        std::cerr << "OgreMeshLoader: no vertex buffer bound for POSITION's source\n";
        return false;
    }
    const VertexBufferBind* normBuf = findBuffer(normEl);
    const VertexBufferBind* uvBuf = findBuffer(uvEl);

    if (verbose)
        std::cout << "  geometry: " << vertexCount << " verts, "
                  << (normBuf ? "normals " : "") << (uvBuf ? "uv0 " : "") << "\n";

    outVerts.resize(vertexCount);
    for (u32 i = 0; i < vertexCount; ++i)
    {
        Vertex v{};
        memcpy(&v.x, posBuf->data.data() + (size_t)i * posBuf->vertexSize + posEl->offset,
               sizeof(float) * 3);
        if (normBuf)
            memcpy(&v.nx, normBuf->data.data() + (size_t)i * normBuf->vertexSize + normEl->offset,
                   sizeof(float) * 3);
        if (uvBuf)
            memcpy(&v.u, uvBuf->data.data() + (size_t)i * uvBuf->vertexSize + uvEl->offset,
                   sizeof(float) * 2);
        outVerts[i] = v;
    }
    return true;
}

u32 FindOrAddMaterial(SimpleMesh* mesh, const std::string& name)
{
    for (u32 i = 0; i < mesh->GetMaterialCount(); ++i)
        if (mesh->GetMaterial(i)->name == name) return i;
    mesh->AddMaterial(name);
    return mesh->GetMaterialCount() - 1;
}

// M_SUBMESH: material name, index buffer, then either a reference to the
// mesh's shared geometry or its own dedicated M_GEOMETRY. Any trailing
// optional chunks (operation type, bone assignments, texture aliases) are
// skipped in bulk via the chunk's own declared end — none of them matter
// for a static, unskinned, triangle-list mesh.
bool ReadSubMesh(ByteReader& r, size_t endPos, SimpleMesh* mesh, const std::vector<Vertex>* shared,
                 bool verbose)
{
    std::string materialName = r.ReadString();
    bool useSharedVertices = r.ReadBool();
    u32 indexCount = r.ReadU32();
    bool indexes32Bit = r.ReadBool();

    std::vector<u32> indices(indexCount);
    for (u32 i = 0; i < indexCount; ++i)
        indices[i] = indexes32Bit ? r.ReadU32() : (u32)r.ReadU16();

    std::vector<Vertex> ownVerts;
    const std::vector<Vertex>* verts = nullptr;
    if (useSharedVertices)
    {
        if (!shared)
        {
            std::cerr << "OgreMeshLoader: submesh '" << materialName
                      << "' uses shared vertices but the mesh declared none\n";
            return false;
        }
        verts = shared;
    }
    else
    {
        if (r.Tell() >= endPos)
        {
            std::cerr << "OgreMeshLoader: submesh '" << materialName
                      << "' has no dedicated geometry\n";
            return false;
        }
        ChunkHeader geomHeader = ReadChunkHeader(r);
        if (geomHeader.id != M_GEOMETRY)
        {
            std::cerr << "OgreMeshLoader: expected M_GEOMETRY in submesh '" << materialName
                      << "', got chunk 0x" << std::hex << geomHeader.id << std::dec << "\n";
            return false;
        }
        size_t geomEnd = r.Tell() + geomHeader.length - 6;
        if (!ReadGeometry(r, geomEnd, ownVerts, verbose)) return false;
        verts = &ownVerts;
    }

    r.Seek(endPos); // skip whatever optional trailing chunks remain

    u32 matIndex = FindOrAddMaterial(mesh, materialName);
    SimpleMeshBuffer* buf = mesh->AddBuffer(matIndex);
    for (const Vertex& v : *verts)
        buf->AddVertex(v);
    for (u32 i = 0; i + 2 < indices.size(); i += 3)
        buf->AddFace(indices[i], indices[i + 1], indices[i + 2]);

    if (verbose)
        std::cout << "  submesh '" << materialName << "': " << verts->size() << " verts, "
                  << indices.size() / 3 << " tris\n";
    return true;
}
} // namespace

bool OgreMeshLoader::Load(const std::string& filename, SimpleMesh* mesh)
{
    std::ifstream f(filename, std::ios::binary | std::ios::ate);
    if (!f)
    {
        std::cerr << "OgreMeshLoader: cannot open '" << filename << "'\n";
        return false;
    }
    size_t size = (size_t)f.tellg();
    f.seekg(0);
    std::vector<u8> data(size);
    f.read((char*)data.data(), (std::streamsize)size);
    f.close();

    ByteReader r(data);

    // M_HEADER: bare u16 id + '\n'-terminated version string, no length field
    u16 headerId = r.ReadU16();
    if (headerId != M_HEADER)
    {
        std::cerr << "OgreMeshLoader: not an Ogre .mesh file (bad header id)\n";
        return false;
    }
    std::string version = r.ReadString();
    if (version != "[MeshSerializer_v1.100]")
    {
        std::cerr << "OgreMeshLoader: unsupported version '" << version
                  << "' — only MeshSerializer_v1.100 (Ogre >= 1.10) is supported\n";
        return false;
    }

    ChunkHeader meshHeader = ReadChunkHeader(r);
    if (meshHeader.id != M_MESH)
    {
        std::cerr << "OgreMeshLoader: expected M_MESH chunk\n";
        return false;
    }
    size_t meshEnd = r.Tell() + meshHeader.length - 6;

    bool skeletallyAnimated = r.ReadBool();
    if (skeletallyAnimated && m_verbose)
        std::cout << "note: mesh is marked skeletally animated — skeleton/bone data will be "
                     "ignored (static-only loader)\n";

    std::vector<Vertex> sharedVertices;
    bool hasShared = false;

    while (r.Tell() < meshEnd)
    {
        ChunkHeader ch = ReadChunkHeader(r);
        size_t childEnd = r.Tell() + ch.length - 6;

        if (ch.id == M_GEOMETRY)
        {
            if (!ReadGeometry(r, childEnd, sharedVertices, m_verbose)) return false;
            hasShared = true;
        }
        else if (ch.id == M_SUBMESH)
        {
            if (!ReadSubMesh(r, childEnd, mesh, hasShared ? &sharedVertices : nullptr, m_verbose))
                return false;
        }
        else
        {
            // skeleton link, bone assignments, LOD levels, bounds, name
            // table, edge lists, poses, animations — all out of scope for
            // a static mesh loader
            r.Seek(childEnd);
        }
    }

    if (mesh->GetBufferCount() == 0)
    {
        std::cerr << "OgreMeshLoader: no submeshes found in '" << filename << "'\n";
        return false;
    }
    return true;
}
