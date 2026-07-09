#pragma once

#include "scene/Math.hpp"
#include "scene/Node.hpp"
#include <vector>

namespace gl
{
class VertexArray;
}
class Material;
class Camera3D;
 
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

    // ── cameras ──
    // Cameras are ordinary nodes in the tree; the scene tracks which one is
    // the ACTIVE one (the main view). Other cameras stay available for extra
    // views — minimap, mirrors, split-screen — each rendered by its own pass
    // over the same collected items.
    void set_active_camera(Camera3D* cam) { m_active_camera = cam; }
    // null when none was set or the node has since left the tree
    Camera3D* get_active_camera();
    Camera3D* find_camera(const std::string& name);    // tree search by name
    void collect_cameras(std::vector<Camera3D*>& out); // every camera in the tree

    // Appends one RenderItem per visible surface. With a frustum, each
    // surface's bounds are transformed to world space and tested — surfaces
    // outside the view are skipped (per-surface culling: one huge mesh with
    // many surfaces still culls piecewise). Null frustum = collect everything.
    void collect(std::vector<RenderItem>& out, const Frustum* frustum = nullptr);

private:
    void collect_node(Node* node, std::vector<RenderItem>& out, const Frustum* frustum);
    void collect_cameras_node(Node* node, std::vector<Camera3D*>& out);

    Node* m_root;
    Camera3D* m_active_camera = nullptr;
};
