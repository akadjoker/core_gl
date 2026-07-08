#include "scene/Scene.hpp"
#include "scene/Mesh.hpp"
#include "scene/MeshInstance.hpp"

Scene::Scene() : m_root(new Node("root")) {}

Scene::~Scene()
{
    delete m_root;
}

void Scene::ready()
{
    m_root->propagate_ready();
}

void Scene::update(float dt)
{
    m_root->propagate_update(dt);
}

void Scene::collect(std::vector<RenderItem>& out, const Frustum* frustum)
{
    collect_node(m_root, out, frustum);
}

void Scene::collect_node(Node* node, std::vector<RenderItem>& out, const Frustum* frustum)
{
    MeshInstance* mi = node->as<MeshInstance>();
    if (mi && mi->visible && mi->get_mesh() && mi->get_mesh()->is_uploaded())
    {
        Mesh* mesh = mi->get_mesh();
        const Mat4& world = mi->get_world_matrix();
        const std::vector<Material*>& mats = mi->get_materials();

        for (u32 s = 0; s < (u32)mesh->surfaces().size(); ++s)
        {
            const Surface& surf = mesh->surfaces()[s];

            if (frustum)
            {
                BoundingBox worldBounds = BoundingBox::TransformBoundingBox(surf.bounds, world);
                if (!frustum->ContainsBox(worldBounds)) continue;
            }

            RenderItem item;
            item.vao = &mesh->vao();
            item.first_index = surf.first_index;
            item.index_count = surf.index_count;
            // per-surface material when provided, else the single one, else null
            if (surf.material_slot >= 0 && surf.material_slot < (int)mats.size())
                item.material = mats[surf.material_slot];
            else if (!mats.empty())
                item.material = mats[0];
            item.world = world;
            out.push_back(item);
        }
    }

    for (Node* child : node->get_children())
        collect_node(child, out, frustum);
}
