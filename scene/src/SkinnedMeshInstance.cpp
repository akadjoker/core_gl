#include "scene/SkinnedMeshInstance.hpp"
#include "scene/SkinnedMesh.hpp"

SkinnedMeshInstance::SkinnedMeshInstance(const std::string& name) : Node3D(name)
{
    m_type = NT_SKINNEDMESH;
}

void SkinnedMeshInstance::set_mesh(SkinnedMesh* shared)
{
    m_shared = shared;
    if (!shared) return;
    const int n = shared->skeleton().bone_count();
    m_locals.resize((size_t)n);
    m_globals.resize((size_t)n);
    m_palette.resize((size_t)n);
    shared->skeleton().bind_pose(m_locals.data());
    m_player.bind(&shared->skeleton(), &shared->clips());
}

int SkinnedMeshInstance::bone_global_index(const char* boneName) const
{
    return m_shared ? m_shared->skeleton().find_bone(boneName) : -1;
}

void SkinnedMeshInstance::_update(float dt)
{
    if (!m_shared || !m_animate || m_locals.empty()) return;
    m_player.update(dt, m_locals.data());
    m_shared->skeleton().evaluate(m_locals.data(), m_globals.data(), m_palette.data());
}

void SkinnedMeshInstance::_release_gpu()
{
    if (m_shared) m_shared->release_gpu();
}
