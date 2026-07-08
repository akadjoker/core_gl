#pragma once

#include "Config.hpp"
#include "Math.hpp"
#include <vector>

// Vértice estático: pos/normal/tangent.w/uv (48B). A bitangente NÃO é guardada
// — reconstrói-se no shader: cross(normal, tangent.xyz) * tangent.w (glTF).
struct MeshVertex
{
    Vec3 position;
    Vec3 normal;
    Vec4 tangent; // w = handedness (+1/-1)
    Vec2 uv;
};

// Parte da mesh com o seu slot de material (multi-material). first_index/
// index_count indexam o index buffer; o render desenha cada surface no range.
struct Surface
{
    u32 first_index = 0;
    u32 index_count = 0;
    int material_slot = 0;
    BoundingBox bounds;
};

class VertexArray;

// RECURSO de geometria estática. NÃO é node, NÃO se desenha, NÃO sabe de
// shader/pass/estado GL. Dono do MeshManager. O render lê gpu()+surfaces() e
// monta RenderItems. Animação e terreno = outras meshes, por partes, depois.
class Mesh
{
public:
    Mesh();
    ~Mesh();
    Mesh(const Mesh &) = delete;
    Mesh &operator=(const Mesh &) = delete;

    // ── construção (CPU) ──
    void set_data(const MeshVertex *verts, u32 vcount, const u16 *indices,
                  u32 icount);
    void set_data(const MeshVertex *verts, u32 vcount, const u32 *indices,
                  u32 icount); // u32: meshes grandes (>65k verts, ex.: Sponza)
    void add_surface(u32 first_index, u32 index_count, int material_slot = 0);
    void add_surface(u32 first_index, u32 index_count, int material_slot,
                     const BoundingBox &bounds);
    void compute_normals();
    void compute_tangents();
    void compute_bounds();

    // ── GPU ──
    void upload();   // constrói o VertexArray a partir dos dados CPU
    void free_cpu(); // liberta a cópia CPU (mantém a GPU)
    bool is_uploaded() const { return m_uploaded; }

    // ── leitura para o render ──
    const VertexArray &gpu() const { return *m_va; }
    const std::vector<Surface> &surfaces() const { return m_surfaces; }
    const std::vector<MeshVertex> &vertices() const { return m_vertices; } // p/ save
    const std::vector<u32> &indices() const { return m_indices; }           // p/ save
    const BoundingBox &bounds() const { return m_bounds; }
    u32 vertex_count() const { return (u32)m_vertices.size(); }
    u32 index_count() const { return (u32)m_indices.size(); }

private:
    std::vector<MeshVertex> m_vertices;
    std::vector<u32> m_indices; // sempre u32 (IB criado 32-bit no upload)
    std::vector<Surface> m_surfaces;
    BoundingBox m_bounds;
    VertexArray *m_va;
    bool m_uploaded;
};
