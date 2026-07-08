#include "pch.h"
#include "Mesh.hpp"
#include "Vertex.hpp"

Mesh::Mesh() : m_bounds(), m_va(new VertexArray()), m_uploaded(false) {}

Mesh::~Mesh() { delete m_va; }

void Mesh::set_data(const MeshVertex *verts, u32 vcount, const u16 *indices,
                    u32 icount)
{
    m_vertices.assign(verts, verts + vcount);
    m_indices.assign(indices, indices + icount); // u16 -> u32
    m_surfaces.clear();
    compute_bounds();
}

void Mesh::set_data(const MeshVertex *verts, u32 vcount, const u32 *indices,
                    u32 icount)
{
    m_vertices.assign(verts, verts + vcount);
    m_indices.assign(indices, indices + icount);
    m_surfaces.clear();
    compute_bounds();
}

void Mesh::add_surface(u32 first_index, u32 index_count, int material_slot)
{
    Surface s;
    s.first_index = first_index;
    s.index_count = index_count;
    s.material_slot = material_slot;
    m_surfaces.push_back(s);
}

void Mesh::add_surface(u32 first_index, u32 index_count, int material_slot,
                       const BoundingBox &bounds)
{
    Surface s;
    s.first_index = first_index;
    s.index_count = index_count;
    s.material_slot = material_slot;
    s.bounds = bounds;
    m_surfaces.push_back(s);
}

void Mesh::compute_bounds()
{
    if (m_vertices.empty())
    {
        m_bounds = BoundingBox{};
        return;
    }
    Vec3 mn = m_vertices[0].position;
    Vec3 mx = mn;
    for (const MeshVertex &v : m_vertices)
    {
        mn = mn.Min(v.position);
        mx = mx.Max(v.position);
    }
    m_bounds = BoundingBox(mn, mx);
}

void Mesh::compute_normals()
{
    for (MeshVertex &v : m_vertices) v.normal = Vec3(0.0f, 0.0f, 0.0f);

    for (size_t i = 0; i + 2 < m_indices.size(); i += 3)
    {
        u32 a = m_indices[i], b = m_indices[i + 1], c = m_indices[i + 2];
        Vec3 n = Vec3::Cross(m_vertices[b].position - m_vertices[a].position,
                             m_vertices[c].position - m_vertices[a].position);
        m_vertices[a].normal += n;
        m_vertices[b].normal += n;
        m_vertices[c].normal += n;
    }
    for (MeshVertex &v : m_vertices) v.normal = v.normal.normalized();
}

void Mesh::compute_tangents()
{
    const size_t n = m_vertices.size();
    std::vector<Vec3> tan(n, Vec3(0.0f, 0.0f, 0.0f));
    std::vector<Vec3> bit(n, Vec3(0.0f, 0.0f, 0.0f));

    for (size_t i = 0; i + 2 < m_indices.size(); i += 3)
    {
        u32 ia = m_indices[i], ib = m_indices[i + 1], ic = m_indices[i + 2];
        const MeshVertex &A = m_vertices[ia];
        const MeshVertex &B = m_vertices[ib];
        const MeshVertex &C = m_vertices[ic];

        Vec3 e1 = B.position - A.position;
        Vec3 e2 = C.position - A.position;
        Vec2 d1 = B.uv - A.uv;
        Vec2 d2 = C.uv - A.uv;

        float denom = d1.x * d2.y - d2.x * d1.y;
        float r = (fabsf(denom) < 1e-8f) ? 0.0f : 1.0f / denom;
        Vec3 t = (e1 * d2.y - e2 * d1.y) * r;
        Vec3 bt = (e2 * d1.x - e1 * d2.x) * r;

        tan[ia] += t; tan[ib] += t; tan[ic] += t;
        bit[ia] += bt; bit[ib] += bt; bit[ic] += bt;
    }

    for (size_t i = 0; i < n; ++i)
    {
        Vec3 nrm = m_vertices[i].normal;
        // Gram-Schmidt: ortogonaliza a tangente contra a normal.
        Vec3 t = tan[i] - nrm * Vec3::Dot(nrm, tan[i]);
        if (t.length_squared() > 1e-12f)
            t.normalize();
        else
            t = Vec3(1.0f, 0.0f, 0.0f);
        float w = (Vec3::Dot(Vec3::Cross(nrm, tan[i]), bit[i]) < 0.0f) ? -1.0f
                                                                       : 1.0f;
        m_vertices[i].tangent = Vec4(t, w);
    }
}

void Mesh::upload()
{
    if (m_vertices.empty() || m_indices.empty()) return;

    VertexBuffer *vb =
        m_va->AddVertexBuffer(sizeof(MeshVertex), (u32)m_vertices.size(), false);
    vb->SetData(m_vertices.data());

    // Ordem de inserção = location: pos=0, normal=1, tangent=2, uv=3.
    VertexDeclaration *d = m_va->GetVertexDeclaration();
    d->AddElement(0, 0, VET_FLOAT3, VES_POSITION);
    d->AddElement(0, 12, VET_FLOAT3, VES_NORMAL);
    d->AddElement(0, 24, VET_FLOAT4, VES_TANGENT);
    d->AddElement(0, 40, VET_FLOAT2, VES_TEXCOORD);

    IndexBuffer *ib =
        m_va->CreateIndexBuffer((u32)m_indices.size(), false, false); // 32-bit
    ib->SetData(m_indices.data());

    m_va->Build();

    // Surface única auto-gerada -> bounds da mesh toda. Se já há surfaces
    // (multi-material, ex.: OBJ), respeita os bounds por surface do loader.
    if (m_surfaces.empty())
    {
        add_surface(0, (u32)m_indices.size(), 0);
        m_surfaces.back().bounds = m_bounds;
    }
    m_uploaded = true;
}

void Mesh::free_cpu()
{
    m_vertices.clear();
    m_vertices.shrink_to_fit();
    m_indices.clear();
    m_indices.shrink_to_fit();
}
