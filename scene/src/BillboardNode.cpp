#include "scene/BillboardNode.hpp"

BillboardNode::BillboardNode(const std::string& name) : Node3D(name) { m_type = NT_BILLBOARD; }

bool BillboardNode::ensure_gpu()
{
    if (m_gpu_ready) return true;

    std::vector<MeshVertex> verts(4);
    u32 indices[6] = {0, 1, 2, 0, 2, 3};
    m_mesh.set_data(verts.data(), 4, indices, 6);
    m_mesh.upload_dynamic();
    m_gpu_ready = true;
    return true;
}

// Bakes this billboard's single quad in world space, MeshVertex.tangent
// carrying RGBA color (same convention ParticleSystemNode/RibbonTrailNode
// use — the particle shader reads it as such, not as a real tangent).
void BillboardNode::rebuild(const Vec3& camRight, const Vec3& camUp, const Vec3& camForward)
{
    if (!ensure_gpu()) return;

    Vec3 pos = get_global_position();
    Vec3 right, up;

    if (m_viewMode == BillboardViewMode::Free)
    {
        right = camRight;
        up = camUp;
    }
    else if (m_viewMode == BillboardViewMode::Upright)
    {
        // yaw-only facing: flatten the camera's forward onto the world
        // XZ plane, keep world-up fixed — doesn't tilt as the camera
        // looks up/down, same as a tree/grass billboard
        Vec3 worldUp(0.f, 1.f, 0.f);
        Vec3 flatForward(camForward.x, 0.f, camForward.z);
        float len = flatForward.length();
        if (len > 1e-5f) flatForward = flatForward * (1.f / len);
        else flatForward = Vec3(0.f, 0.f, 1.f); // camera looking straight down/up — arbitrary facing
        right = Vec3::Cross(flatForward, worldUp);
        len = right.length();
        right = len > 1e-5f ? right * (1.f / len) : Vec3(1.f, 0.f, 0.f);
        up = worldUp;
    }
    else // Fixed: the node's own orientation, no camera-facing at all
    {
        Mat4 rot(get_global_rotation());
        right = rot * Vec3(1.f, 0.f, 0.f);
        up = rot * Vec3(0.f, 1.f, 0.f);
    }

    Vec3 hr = right * (m_size.x * 0.5f);
    Vec3 hu = up * (m_size.y * 0.5f);
    Vec4 uvRect = m_animated ? m_animator.uv_rect() : m_uvRect;
    float u0 = uvRect.x, v0 = uvRect.y;
    float u1 = uvRect.x + uvRect.z, v1 = uvRect.y + uvRect.w;

    MeshVertex verts[4];
    verts[0].position = pos - hr - hu;
    verts[0].uv = Vec2(u0, v1);
    verts[0].tangent = m_color;
    verts[1].position = pos + hr - hu;
    verts[1].uv = Vec2(u1, v1);
    verts[1].tangent = m_color;
    verts[2].position = pos + hr + hu;
    verts[2].uv = Vec2(u1, v0);
    verts[2].tangent = m_color;
    verts[3].position = pos - hr + hu;
    verts[3].uv = Vec2(u0, v0);
    verts[3].tangent = m_color;

    m_mesh.update_vertices(verts, 4);
}
