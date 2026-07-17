#include "scene/MorphMeshInstance.hpp"

MorphMeshInstance::MorphMeshInstance(const std::string& name) : MeshInstance(name) {}

void MorphMeshInstance::set_anim_data(Mesh* mesh, MorphKeyframes keyframes, MorphTags tags)
{
    set_mesh(mesh);
    m_keyframes = std::move(keyframes);
    m_tags = std::move(tags);
    m_work = mesh ? mesh->vertices() : std::vector<MeshVertex>(); // uv/tangent stay fixed from here on
}

bool MorphMeshInstance::get_tag_transform(const char* tagName, Mat4& out) const
{
    int idx = m_tags.find(tagName);
    if (idx < 0)
    {
        out = Mat4::Identity();
        return false;
    }
    out = m_animator.tag_transform(m_tags, idx);
    return true;
}

void MorphMeshInstance::_update(float dt)
{
    Mesh* mesh = get_mesh();
    if (!mesh || m_work.empty()) return;

    m_animator.update(dt);
    m_animator.write_vertices(m_keyframes, m_work);
    mesh->update_vertices(m_work.data(), (u32)m_work.size());
}
