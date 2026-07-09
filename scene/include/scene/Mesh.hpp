#pragma once

#include "scene/Math.hpp"
#include <coregl/gl_buffer.hpp>
#include <coregl/gl_vertex_array.hpp>
#include <vector>

 
struct MeshVertex
{
    Vec3 position;
    Vec3 normal;
    Vec4 tangent; // w = handedness (+1/-1)
    Vec2 uv;
};

// A slice of the mesh with its material slot (multi-material meshes).
// first_index/index_count address the index buffer; the renderer draws each
// surface as one ranged DrawIndexed.
struct Surface
{
    u32 first_index = 0;
    u32 index_count = 0;
    int material_slot = 0;
    BoundingBox bounds;
};

 
class Mesh
{
public:
    Mesh() = default;
    Mesh(const Mesh&) = delete;
    Mesh& operator=(const Mesh&) = delete;

    // ── CPU-side construction ──
    void set_data(const MeshVertex* verts, u32 vcount, const u16* indices, u32 icount);
    void set_data(const MeshVertex* verts, u32 vcount, const u32* indices,
                  u32 icount); // u32: big meshes (>65k verts, e.g. Sponza)
    void add_surface(u32 first_index, u32 index_count, int material_slot = 0);
    void add_surface(u32 first_index, u32 index_count, int material_slot,
                     const BoundingBox& bounds);
    void compute_normals();
    void compute_tangents();
    void compute_bounds();

    // ── GPU ──
    void upload();      // builds the GL buffers/VAO from the CPU data
    void free_cpu();    // drops the CPU copy (GPU side stays)
    void release_gpu(); // frees the GL objects (call while the context lives)
    bool is_uploaded() const { return m_uploaded; }

    // ── read access for the renderer ──
    gl::VertexArray& vao() { return m_vao; }
    const std::vector<Surface>& surfaces() const { return m_surfaces; }
    const std::vector<MeshVertex>& vertices() const { return m_vertices; } // for save
    const std::vector<u32>& indices() const { return m_indices; }          // for save
    const BoundingBox& bounds() const { return m_bounds; }
    u32 vertex_count() const { return (u32)m_vertices.size(); }
    u32 index_count() const { return (u32)m_indices.size(); }

private:
    std::vector<MeshVertex> m_vertices;
    std::vector<u32> m_indices; // always u32 (uploaded as a 32-bit IB)
    std::vector<Surface> m_surfaces;
    BoundingBox m_bounds;
    gl::Buffer m_vbo;
    gl::Buffer m_ibo;
    gl::VertexArray m_vao;
    bool m_uploaded = false;
};
