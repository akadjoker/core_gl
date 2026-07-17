#include "scene/Mesh.hpp"
#include "scene/Material.hpp"

Mesh::~Mesh()
{
    for (Material* m : m_materials)
        delete m;
}

void Mesh::set_data(const MeshVertex* verts, u32 vcount, const u16* indices, u32 icount)
{
    m_vertices.assign(verts, verts + vcount);
    m_indices.assign(indices, indices + icount); // u16 -> u32
    m_surfaces.clear();
    compute_bounds();
}

void Mesh::set_data(const MeshVertex* verts, u32 vcount, const u32* indices, u32 icount)
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
                       const BoundingBox& bounds)
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
    for (const MeshVertex& v : m_vertices)
    {
        mn = mn.Min(v.position);
        mx = mx.Max(v.position);
    }
    m_bounds = BoundingBox(mn, mx);
}

void Mesh::compute_normals()
{
    for (MeshVertex& v : m_vertices)
        v.normal = Vec3(0.0f, 0.0f, 0.0f);

    for (size_t i = 0; i + 2 < m_indices.size(); i += 3)
    {
        u32 a = m_indices[i], b = m_indices[i + 1], c = m_indices[i + 2];
        Vec3 n = Vec3::Cross(m_vertices[b].position - m_vertices[a].position,
                             m_vertices[c].position - m_vertices[a].position);
        m_vertices[a].normal += n;
        m_vertices[b].normal += n;
        m_vertices[c].normal += n;
    }
    for (MeshVertex& v : m_vertices)
        v.normal = v.normal.normalized();
}

void Mesh::compute_tangents()
{
    const size_t n = m_vertices.size();
    std::vector<Vec3> tan(n, Vec3(0.0f, 0.0f, 0.0f));
    std::vector<Vec3> bit(n, Vec3(0.0f, 0.0f, 0.0f));

    for (size_t i = 0; i + 2 < m_indices.size(); i += 3)
    {
        u32 ia = m_indices[i], ib = m_indices[i + 1], ic = m_indices[i + 2];
        const MeshVertex& A = m_vertices[ia];
        const MeshVertex& B = m_vertices[ib];
        const MeshVertex& C = m_vertices[ic];

        Vec3 e1 = B.position - A.position;
        Vec3 e2 = C.position - A.position;
        Vec2 d1 = B.uv - A.uv;
        Vec2 d2 = C.uv - A.uv;

        float denom = d1.x * d2.y - d2.x * d1.y;
        float r = (fabsf(denom) < 1e-8f) ? 0.0f : 1.0f / denom;
        Vec3 t = (e1 * d2.y - e2 * d1.y) * r;
        Vec3 bt = (e2 * d1.x - e1 * d2.x) * r;

        tan[ia] += t;
        tan[ib] += t;
        tan[ic] += t;
        bit[ia] += bt;
        bit[ib] += bt;
        bit[ic] += bt;
    }

    for (size_t i = 0; i < n; ++i)
    {
        Vec3 nrm = m_vertices[i].normal;
        // Gram-Schmidt: orthogonalize the tangent against the normal
        Vec3 t = tan[i] - nrm * Vec3::Dot(nrm, tan[i]);
        if (t.length_squared() > 1e-12f)
            t.normalize();
        else
            t = Vec3(1.0f, 0.0f, 0.0f);
        float w = (Vec3::Dot(Vec3::Cross(nrm, tan[i]), bit[i]) < 0.0f) ? -1.0f : 1.0f;
        m_vertices[i].tangent = Vec4(t, w);
    }
}

void Mesh::set_lightmap_uvs(const Vec2* uvs, u32 count)
{
    if (!uvs || count == 0 || count != (u32)m_vertices.size()) return;
    m_uv2_vbo.Allocate(gl::BufferType::ARRAY, uvs, count * sizeof(Vec2),
                       gl::UsageType::STATIC_DRAW);
    m_has_lightmap_uvs = true;
}

void Mesh::upload()
{
    if (m_vertices.empty() || m_indices.empty()) return;

    m_vbo.Allocate(gl::BufferType::ARRAY, m_vertices.data(), m_vertices.size() * sizeof(MeshVertex),
                   gl::UsageType::STATIC_DRAW);
    m_ibo.Allocate(gl::BufferType::ELEMENT_ARRAY, m_indices.data(), m_indices.size() * sizeof(u32),
                   gl::UsageType::STATIC_DRAW);

    // attribute order = shader location: pos=0, normal=1, tangent=2, uv=3
    const gl::VertexAttrib layout[] = {
        {gl::VertexAttribType::FLOAT, 3, 0, false}, // position
        {gl::VertexAttribType::FLOAT, 3, 0, false}, // normal
        {gl::VertexAttribType::FLOAT, 4, 0, false}, // tangent (w = handedness)
        {gl::VertexAttribType::FLOAT, 2, 0, false}, // uv
    };
    m_vao.AddVertexBuffer(m_vbo, layout, 4, sizeof(MeshVertex));

    // Optional lightmap UVs (BSP meshes only — does NOT change MeshVertex layout)
    if (m_has_lightmap_uvs)
    {
        const gl::VertexAttrib uv2Layout = {gl::VertexAttribType::FLOAT, 2, 0, false};
        m_vao.AddVertexBuffer(m_uv2_vbo, &uv2Layout, 1, sizeof(Vec2));
    }

    m_vao.SetIndexBuffer(m_ibo, gl::VertexAttribType::UINT);

    // auto-generate a single whole-mesh surface when the loader didn't
    // provide any (multi-material formats like OBJ set their own)
    if (m_surfaces.empty())
    {
        add_surface(0, (u32)m_indices.size(), 0);
        m_surfaces.back().bounds = m_bounds;
    }
    m_uploaded = true;
}

void Mesh::upload_dynamic()
{
    if (m_vertices.empty() || m_indices.empty()) return;

    m_dynamic = true;
    m_vbo.Allocate(gl::BufferType::ARRAY, m_vertices.data(), m_vertices.size() * sizeof(MeshVertex),
                   gl::UsageType::DYNAMIC_DRAW);
    m_ibo.Allocate(gl::BufferType::ELEMENT_ARRAY, m_indices.data(), m_indices.size() * sizeof(u32),
                   gl::UsageType::DYNAMIC_DRAW);

    const gl::VertexAttrib layout[] = {
        {gl::VertexAttribType::FLOAT, 3, 0, false}, // position
        {gl::VertexAttribType::FLOAT, 3, 0, false}, // normal
        {gl::VertexAttribType::FLOAT, 4, 0, false}, // tangent (w = handedness)
        {gl::VertexAttribType::FLOAT, 2, 0, false}, // uv
    };
    m_vao.AddVertexBuffer(m_vbo, layout, 4, sizeof(MeshVertex));

    if (m_has_lightmap_uvs)
    {
        const gl::VertexAttrib uv2Layout = {gl::VertexAttribType::FLOAT, 2, 0, false};
        m_vao.AddVertexBuffer(m_uv2_vbo, &uv2Layout, 1, sizeof(Vec2));
    }

    m_vao.SetIndexBuffer(m_ibo, gl::VertexAttribType::UINT);

    if (m_surfaces.empty())
    {
        add_surface(0, (u32)m_indices.size(), 0);
        m_surfaces.back().bounds = m_bounds;
    }
    m_uploaded = true;
}

void Mesh::update_indices(const u32* indices, u32 icount)
{
    if (!m_uploaded || !m_dynamic || !indices || icount == 0) return;
    if ((size_t)icount * sizeof(u32) > m_indices.size() * sizeof(u32)) return; // fits allocation
    m_ibo.Upload(indices, (size_t)icount * sizeof(u32));
}

void Mesh::update_vertices(const MeshVertex* verts, u32 vcount)
{
    if (!m_uploaded || !m_dynamic || !verts || vcount == 0) return;
    if ((size_t)vcount * sizeof(MeshVertex) > m_vertices.size() * sizeof(MeshVertex))
        return; // fits allocation
    m_vbo.Upload(verts, (size_t)vcount * sizeof(MeshVertex));
}

void Mesh::set_dynamic_index_count(u32 icount)
{
    if (!m_surfaces.empty()) m_surfaces[0].index_count = icount;
}

bool Mesh::set_material_texture(int slot, gl::Texture* tex)
{
    if (slot < 0 || slot >= (int)m_materials.size()) return false;
    Material* m = get_material(slot);
    if (!m) return false;
    m->diffuse = tex;
    return true;
}

void Mesh::release_gpu()
{
    m_vao.Release();
    m_vbo.Release();
    m_ibo.Release();
    m_uploaded = false;
}

void Mesh::free_cpu()
{
    m_vertices.clear();
    m_vertices.shrink_to_fit();
    m_indices.clear();
    m_indices.shrink_to_fit();
}
