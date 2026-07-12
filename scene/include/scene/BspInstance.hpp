#pragma once

#include "scene/Node3D.hpp"
#include <vector>

class Mesh;
class Material;

// BSP lightmapped mesh instance.  Identical API to MeshInstance but the
// renderer collects it separately and draws it with the BSP shader
// (uv2 lightmap, no tangents, no CSM shadows).
class BspInstance : public Node3D
{
public:
    static constexpr NodeType ClassType = NT_BSPINSTANCE;

    explicit BspInstance(const std::string& name = "BspInstance")
        : Node3D(name), m_mesh(nullptr)
    {
        m_type = NT_BSPINSTANCE;
    }

    bool is_a(NodeType t) const override { return t == NT_BSPINSTANCE || Node3D::is_a(t); }

    void set_mesh(Mesh* mesh) { m_mesh = mesh; }
    Mesh* get_mesh() const { return m_mesh; }

    void set_material(Material* m) { m_materials.assign(1, m); }
    void set_materials(const std::vector<Material*>& m) { m_materials = m; }
    const std::vector<Material*>& get_materials() const { return m_materials; }

    bool visible = true;

private:
    Mesh* m_mesh;
    std::vector<Material*> m_materials;
};
