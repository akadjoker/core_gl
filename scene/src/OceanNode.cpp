#include "scene/OceanNode.hpp"

void OceanNode::build_surface(Mesh& mesh)
{
    // dense flat grid: the ocean shader displaces every vertex, so the
    // resolution decides how sharp the waves can be
    const int cells = grid_resolution;
    const int n = cells + 1;
    const float half = get_size();
    const float step = (2.f * half) / cells;

    std::vector<MeshVertex> verts((size_t)n * n);
    for (int j = 0; j < n; ++j)
    {
        for (int i = 0; i < n; ++i)
        {
            MeshVertex& v = verts[(size_t)j * n + i];
            v.position = Vec3(-half + i * step, 0.f, -half + j * step);
            v.normal = Vec3(0.f, 1.f, 0.f);
            v.tangent = Vec4(1.f, 0.f, 0.f, 1.f);
            // uv in world units so the bump tiling is size-independent
            v.uv = Vec2(v.position.x, v.position.z);
        }
    }

    std::vector<u32> idx;
    idx.reserve((size_t)cells * cells * 6);
    for (int j = 0; j < cells; ++j)
    {
        for (int i = 0; i < cells; ++i)
        {
            u32 a = (u32)(j * n + i);
            u32 b = a + 1;
            u32 d = a + (u32)n;
            u32 c = d + 1;
            // CCW seen from above
            idx.push_back(a);
            idx.push_back(c);
            idx.push_back(b);
            idx.push_back(a);
            idx.push_back(d);
            idx.push_back(c);
        }
    }

    mesh.set_data(verts.data(), (u32)verts.size(), idx.data(), (u32)idx.size());
}
