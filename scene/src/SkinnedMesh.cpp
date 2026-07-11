#include "scene/SkinnedMesh.hpp"
#include <coregl/gl_log.hpp>

// TODO(evening): parse the .mesh SKEL chunk (per bone: name cstring,
// parent i32, local 16f, inverseBind 16f) and SKIN chunk (4x boneID u8 +
// 4x weight f32 per vertex) — see exporter/src/MeshWriter.cpp — plus the
// .anim loader (ANIM_MAGIC/ANIM_VERSION/ANIM_CHUNK_INFO). Weights stream
// uploads at locations 4 (ids, ivec4) / 5 (weights, vec4).

SkinnedMesh::~SkinnedMesh()
{
    for (AnimationClip* c : m_clips)
        delete c;
}

bool SkinnedMesh::load(const char* meshPath)
{
    gl::Log::Warn("SkinnedMesh::load('%s'): not implemented yet", meshPath);
    return false; // TODO(evening)
}

bool SkinnedMesh::load_animations(const char* animPath)
{
    gl::Log::Warn("SkinnedMesh::load_animations('%s'): not implemented yet", animPath);
    return false; // TODO(evening)
}

const AnimationClip* SkinnedMesh::find_clip(const std::string& name) const
{
    for (AnimationClip* c : m_clips)
        if (c && c->name() == name) return c;
    return nullptr;
}

bool SkinnedMesh::ensure_gpu()
{
    return false; // TODO(evening): mesh upload + weights stream VBO
}

void SkinnedMesh::release_gpu()
{
    m_mesh.release_gpu();
    m_weightsVbo.Release();
    m_gpuReady = false;
}
