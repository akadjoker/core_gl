#include "scene/MirrorNode.hpp"

bool MirrorNode::ensure_gpu(int reflection_w, int reflection_h)
{
    if (m_gpu_ready) return true;

    m_targetW = reflection_w;
    m_targetH = reflection_h;

    m_reflTex.Load2D(nullptr, reflection_w, reflection_h, gl::TextureFormat::RGBA8);
    m_reflTex.SetFilter(gl::TextureFilter::LINEAR, gl::TextureFilter::LINEAR);
    m_reflTex.SetWrap(gl::TextureWrap::CLAMP_TO_EDGE, gl::TextureWrap::CLAMP_TO_EDGE);
    m_reflDepth.Allocate(reflection_w, reflection_h, gl::TextureFormat::DEPTH24);
    m_reflFbo.AttachTexture(m_reflTex, gl::Attachment::COLOR0);
    m_reflFbo.AttachRenderBuffer(m_reflDepth, gl::Attachment::DEPTH);
    m_reflFbo.SetDrawBuffers();
    if (!m_reflFbo.IsComplete()) return false;

    build_surface(m_quad);
    m_quad.upload();

    m_gpu_ready = true;
    return true;
}

void MirrorNode::build_surface(Mesh& mesh)
{
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
    mesh.set_data(verts, 4, idx, 6);
}

void MirrorNode::release_gpu()
{
    if (!m_gpu_ready) return;
    m_reflFbo.Release();
    m_reflTex.Release();
    m_reflDepth.Release();
    m_quad.release_gpu();
    m_gpu_ready = false;
}
