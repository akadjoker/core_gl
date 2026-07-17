#pragma once

#include "scene/Mesh.hpp" // full definition needed for set_mesh()'s materials() default
#include "scene/Node3D.hpp"
#include <vector>

class Material;

class MeshInstance : public Node3D
{
public:
    static constexpr NodeType ClassType = NT_MESHINSTANCE;

    explicit MeshInstance(const std::string& name = "MeshInstance") : Node3D(name), m_mesh(nullptr)
    {
        m_type = NT_MESHINSTANCE;
    }

    bool is_a(NodeType t) const override { return t == NT_MESHINSTANCE || Node3D::is_a(t); }

    // non-owning. Defaults this instance's materials to the mesh's own
    // (built by MeshLoader::load() from the file's MATS chunk) — set_material()/
    // set_materials() afterward can still override with a custom look.
    void set_mesh(Mesh* mesh)
    {
        m_mesh = mesh;
        if (mesh) m_materials = mesh->materials();
    }
    Mesh* get_mesh() const { return m_mesh; }

    void set_material(Material* m) { m_materials.assign(1, m); } // non-owning
    void set_materials(const std::vector<Material*>& m) { m_materials = m; }
    const std::vector<Material*>& get_materials() const { return m_materials; }

    bool visible = true;

private:
    Mesh* m_mesh;
    std::vector<Material*> m_materials;
};
