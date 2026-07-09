#include "scene/LightNode.hpp"

bool PointLight::ensure_gpu()
{
    if (m_gpu_ready) return true;
    if (!cast_shadows) return false;

    // linear-distance cubemap: the depth pass writes
    // length(frag - light) / range into each face
    m_shadowTex.LoadDepthCube(shadow_resolution);
    m_shadowTex.SetWrap(gl::TextureWrap::CLAMP_TO_EDGE, gl::TextureWrap::CLAMP_TO_EDGE,
                        gl::TextureWrap::CLAMP_TO_EDGE);
    m_shadowFbo.AttachCubeFace(m_shadowTex, gl::Attachment::DEPTH, 0);
    m_shadowFbo.SetDrawBuffers();
    if (!m_shadowFbo.IsComplete()) return false;

    m_gpu_ready = true;
    return true;
}

bool SpotLight::ensure_gpu()
{
    if (m_gpu_ready) return true;
    if (!cast_shadows) return false;

    m_shadowTex.LoadDepth(shadow_resolution, shadow_resolution);
    m_shadowTex.SetWrap(gl::TextureWrap::CLAMP_TO_EDGE, gl::TextureWrap::CLAMP_TO_EDGE);
    m_shadowFbo.AttachTexture(m_shadowTex, gl::Attachment::DEPTH);
    m_shadowFbo.SetDrawBuffers();
    if (!m_shadowFbo.IsComplete()) return false;

    m_gpu_ready = true;
    return true;
}

void LightNode::release_gpu()
{
    if (!m_gpu_ready) return;
    m_shadowFbo.Release();
    m_shadowTex.Release();
    m_gpu_ready = false;
}
