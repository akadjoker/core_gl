#include "scene/Scene.hpp"
#include "scene/Camera3D.hpp"
#include "scene/Material.hpp"
#include "scene/Mesh.hpp"
#include "scene/MeshInstance.hpp"
#include "scene/BspInstance.hpp"
#include "scene/SceneOctree.hpp"

Scene::Scene() : m_root(new Node("root")) {}

Scene::~Scene()
{
    delete m_root;
    for (Mesh* m : m_meshes)
        delete m;
    for (Material* m : m_materials)
        delete m;
}

Mesh* Scene::create_mesh()
{
    m_meshes.push_back(new Mesh());
    return m_meshes.back();
}

Material* Scene::create_material()
{
    m_materials.push_back(new Material());
    return m_materials.back();
}

Material* Scene::create_material(float r, float g, float b)
{
    m_materials.push_back(new Material(Vec3(r, g, b)));
    return m_materials.back();
}

void Scene::release_gpu()
{
    m_root->propagate_release_gpu();
    for (Mesh* m : m_meshes)
        m->release_gpu();
}

void Scene::ready()
{
    m_root->propagate_ready();
}

void Scene::update(float dt)
{
    m_root->propagate_update(dt);
}

Camera3D* Scene::get_active_camera()
{
    // the pointer may outlive the node's membership of this tree (removed or
    // reparented elsewhere) — treat a camera outside the tree as "none"
    if (m_active_camera && !m_root->is_ancestor_of(m_active_camera)) m_active_camera = nullptr;
    return m_active_camera;
}

Camera3D* Scene::find_camera(const std::string& name)
{
    Node* n = m_root->find_child(name, true);
    return n ? n->as<Camera3D>() : nullptr;
}

void Scene::collect_cameras(std::vector<Camera3D*>& out)
{
    collect_cameras_node(m_root, out);
}

void Scene::collect_cameras_node(Node* node, std::vector<Camera3D*>& out)
{
    Camera3D* cam = node->as<Camera3D>();
    if (cam) out.push_back(cam);
    for (Node* child : node->get_children())
        collect_cameras_node(child, out);
}

void Scene::collect(std::vector<RenderItem>& out, const Frustum* frustum)
{
    collect_node(m_root, out, frustum);
}

void Scene::collect(std::vector<RenderItem>& out, const Frustum* frustum, const SceneOctree* octree)
{
    if (!frustum || !octree || octree->empty())
    {
        collect_node(m_root, out, frustum);
        return;
    }

    std::vector<SceneOctreeEntry> candidates;
    octree->queryFrustum(*frustum, candidates);
    for (const SceneOctreeEntry& e : candidates)
    {
        if (!frustum->ContainsBox(e.worldBounds)) continue; // precise per-surface test
        collect_surface(e.instance, e.surfaceIndex, out);
    }
}

void Scene::collect_node(Node* node, std::vector<RenderItem>& out, const Frustum* frustum)
{
    MeshInstance* mi = node->as<MeshInstance>();
    if (mi) collect_instance(mi, out, frustum);

    for (Node* child : node->get_children())
        collect_node(child, out, frustum);
}

void Scene::collect_instance(MeshInstance* mi, std::vector<RenderItem>& out, const Frustum* frustum)
{
    if (!mi->visible || !mi->get_mesh() || !mi->get_mesh()->is_uploaded()) return;

    Mesh* mesh = mi->get_mesh();
    const Mat4& world = mi->get_world_matrix();

    for (u32 s = 0; s < (u32)mesh->surfaces().size(); ++s)
    {
        if (frustum)
        {
            BoundingBox worldBounds = BoundingBox::TransformBoundingBox(mesh->surfaces()[s].bounds, world);
            if (!frustum->ContainsBox(worldBounds)) continue;
        }
        collect_surface(mi, s, out);
    }
}

void Scene::collect_surface(MeshInstance* mi, u32 surfaceIndex, std::vector<RenderItem>& out)
{
    Mesh* mesh = mi->get_mesh();
    const Surface& surf = mesh->surfaces()[surfaceIndex];
    const std::vector<Material*>& mats = mi->get_materials();

    RenderItem item;
    item.vao = &mesh->vao();
    item.first_index = surf.first_index;
    item.index_count = surf.index_count;
    // per-surface material when provided, else the single one, else null
    if (surf.material_slot >= 0 && surf.material_slot < (int)mats.size())
        item.material = mats[surf.material_slot];
    else if (!mats.empty())
        item.material = mats[0];
    item.world = mi->get_world_matrix();
    out.push_back(item);
}

static void collect_bsp_node(Node* node, std::vector<BspInstance*>& out)
{
    BspInstance* bsp = node->as<BspInstance>();
    if (bsp && bsp->visible && bsp->get_mesh() && bsp->get_mesh()->is_uploaded())
        out.push_back(bsp);

    for (Node* child : node->get_children())
        collect_bsp_node(child, out);
}

void Scene::collect_bsp(std::vector<BspInstance*>& out)
{
    collect_bsp_node(m_root, out);
}
