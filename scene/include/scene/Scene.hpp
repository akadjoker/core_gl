#pragma once

#include "scene/Math.hpp"
#include "scene/Node.hpp"
#include <vector>

namespace gl
{
class VertexArray;
}
class Material;

// One drawable unit, flattened out of the node tree. Deliberately at the
// DRAW-COMMAND level (geometry range + material + world), not the resource
// level: a pass never needs to know whether the geometry came from a static
// Mesh surface, a terrain chunk with its own VAO, or a skinned mesh — they
// all collapse to the same item. Skinned geometry additionally carries its
// bone palette; a pass that sees skin_palette != null binds the skinned
// shader variant and uploads the matrices. The world matrix is copied at
// collect time so passes run against a stable list.
struct RenderItem
{
    gl::VertexArray* vao = nullptr;
    u32 first_index = 0;
    u32 index_count = 0;
    Material* material = nullptr;
    Mat4 world;
    const Mat4* skin_palette = nullptr; // bone matrices; null = static geometry
    u32 skin_count = 0;
};

// Owns the node tree and turns it into flat render lists. The tree is the
// AUTHORING structure (logic, hierarchy, transforms); rendering never walks
// it directly — every frame collect() gathers visible MeshInstances into a
// RenderItem list that passes (forward, shadow cascades, reflections...)
// consume. Each view culls with its own frustum: the camera pass with the
// camera's, each shadow cascade with the light's — same tree, different
// frustum, different list.
class Scene
{
public:
    Scene();
    ~Scene();
    Scene(const Scene&) = delete;
    Scene& operator=(const Scene&) = delete;

    Node& root() { return *m_root; }

    // lifecycle fan-out over the whole tree
    void ready();
    void update(float dt);

    // Appends one RenderItem per visible surface. With a frustum, each
    // surface's bounds are transformed to world space and tested — surfaces
    // outside the view are skipped (per-surface culling: one huge mesh with
    // many surfaces still culls piecewise). Null frustum = collect everything.
    void collect(std::vector<RenderItem>& out, const Frustum* frustum = nullptr);

private:
    void collect_node(Node* node, std::vector<RenderItem>& out, const Frustum* frustum);

    Node* m_root;
};
