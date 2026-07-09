#include "scene/WaterNode.hpp"

static bool buildTarget(gl::FrameBuffer& fbo, gl::Texture& color, gl::RenderBuffer& depth, int w,
                        int h)
{
    color.Load2D(nullptr, w, h, gl::TextureFormat::RGBA8);
    color.SetFilter(gl::TextureFilter::LINEAR, gl::TextureFilter::LINEAR);
    color.SetWrap(gl::TextureWrap::CLAMP_TO_EDGE, gl::TextureWrap::CLAMP_TO_EDGE);
    depth.Allocate(w, h, gl::TextureFormat::DEPTH24);

    fbo.AttachTexture(color, gl::Attachment::COLOR0);
    fbo.AttachRenderBuffer(depth, gl::Attachment::DEPTH);
    fbo.SetDrawBuffers();
    return fbo.IsComplete();
}

bool WaterNode::ensure_gpu(int reflection_w, int reflection_h)
{
    if (m_gpu_ready) return true;

    m_targetW = reflection_w;
    m_targetH = reflection_h;
    if (!buildTarget(m_reflFbo, m_reflTex, m_reflDepth, reflection_w, reflection_h)) return false;

    // refraction keeps its depth in a TEXTURE so the water shader can read
    // the scene depth per pixel (soft shoreline, depth-tinted color)
    m_refrTex.Load2D(nullptr, reflection_w, reflection_h, gl::TextureFormat::RGBA8);
    m_refrTex.SetFilter(gl::TextureFilter::LINEAR, gl::TextureFilter::LINEAR);
    m_refrTex.SetWrap(gl::TextureWrap::CLAMP_TO_EDGE, gl::TextureWrap::CLAMP_TO_EDGE);
    m_refrDepthTex.LoadDepth(reflection_w, reflection_h, gl::TextureFormat::DEPTH24);
    m_refrFbo.AttachTexture(m_refrTex, gl::Attachment::COLOR0);
    m_refrFbo.AttachTexture(m_refrDepthTex, gl::Attachment::DEPTH);
    m_refrFbo.SetDrawBuffers();
    if (!m_refrFbo.IsComplete()) return false;

    // the surface quad, on XZ at local y=0 (world height comes from the node)
    const float h = m_half;
    MeshVertex verts[4];
    const float pos[4][3] = {{-h, 0, -h}, {h, 0, -h}, {h, 0, h}, {-h, 0, h}};
    for (int i = 0; i < 4; ++i)
    {
        verts[i].position = Vec3(pos[i][0], pos[i][1], pos[i][2]);
        verts[i].normal = Vec3(0.f, 1.f, 0.f);
        verts[i].tangent = Vec4(1.f, 0.f, 0.f, 1.f);
        verts[i].uv = Vec2((float)(i == 1 || i == 2), (float)(i >= 2));
    }
    const u16 idx[6] = {0, 2, 1, 0, 3, 2}; // CCW seen from above
    m_quad.set_data(verts, 4, idx, 6);
    m_quad.upload();

    m_gpu_ready = true;
    return true;
}

void WaterNode::release_gpu()
{
    if (!m_gpu_ready) return;
    m_reflFbo.Release();
    m_refrFbo.Release();
    m_reflTex.Release();
    m_refrTex.Release();
    m_refrDepthTex.Release();
    m_reflDepth.Release();
    m_quad.release_gpu();
    m_gpu_ready = false;
}
