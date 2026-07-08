#pragma once

#include "scene/Node3D.hpp"
#include <vector>

class Mesh;
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

    void set_mesh(Mesh* mesh) { m_mesh = mesh; } // non-owning
    Mesh* get_mesh() const { return m_mesh; }

    void set_material(Material* m) { m_materials.assign(1, m); } // non-owning
    void set_materials(const std::vector<Material*>& m) { m_materials = m; }
    const std::vector<Material*>& get_materials() const { return m_materials; }

    bool visible = true;

private:
    Mesh* m_mesh;
    std::vector<Material*> m_materials;
};
