#include "scene/GrassSystemNode.hpp"
#include <cmath>
#include <random>

GrassSystemNode::GrassSystemNode(const std::string& name, int maxClumps, GrassType type)
    : Node3D(name), m_grassType(type), m_maxClumps(maxClumps)
{
    m_quadsPerClump = (type == GrassType::TriCross) ? 3 : (type == GrassType::Cross ? 2 : 1);
    m_type = NT_GRASSSYSTEM;
}

void GrassSystemNode::addClump(const Vec3& pos, const Vec2& size, const Vec4& color)
{
    if ((int)m_clumps.size() < m_maxClumps) m_clumps.push_back({pos, size, color});
}

void GrassSystemNode::fillArea(const Vec3& center, float w, float d, int count, float mn, float mx,
                               unsigned seed)
{
    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> u(0.f, 1.f);
    for (int i = 0; i < count; ++i)
    {
        Vec3 p = center + Vec3((u(rng) - 0.5f) * w, 0.f, (u(rng) - 0.5f) * d);
        float s = mn + u(rng) * (mx - mn);
        Vec4 c(0.4f + u(rng) * 0.3f, 0.6f + u(rng) * 0.3f, 0.2f + u(rng) * 0.1f, 1.f);
        addClump(p, Vec2(s * 0.8f, s), c);
    }
}

void GrassSystemNode::addQuad(std::vector<MeshVertex>& v, const Vec3& center, const Vec3& right,
                              const Vec3& up, const Vec2& size, const Vec4& color)
{
    float hw = size.x * 0.5f, h = size.y;
    auto push = [&](const Vec3& pos, const Vec2& uv)
    {
        MeshVertex mv;
        mv.position = pos;
        mv.normal = Vec3(0.f, 1.f, 0.f);
        mv.tangent = color; // color rides the unused tangent slot (see header)
        mv.uv = uv;
        v.push_back(mv);
    };
    push(center - right * hw, Vec2(0, 1));
    push(center + right * hw, Vec2(1, 1));
    push(center + right * hw + up * h, Vec2(1, 0));
    push(center - right * hw + up * h, Vec2(0, 0));
}

void GrassSystemNode::build()
{
    std::vector<MeshVertex> verts;
    const Vec3 worldUp(0, 1, 0);
    for (const Clump& c : m_clumps)
    {
        switch (m_grassType)
        {
            case GrassType::TriCross:
            {
                const float deg[3] = {0.f, 60.f, 120.f};
                for (float a : deg)
                {
                    float r = a * 3.14159265f / 180.f;
                    Vec3 axis(cosf(r), 0.f, sinf(r));
                    addQuad(verts, c.pos, axis, worldUp, c.size, c.color);
                }
                break;
            }
            case GrassType::Cross:
                addQuad(verts, c.pos, Vec3(1, 0, 0), worldUp, c.size, c.color);
                addQuad(verts, c.pos, Vec3(0, 0, 1), worldUp, c.size, c.color);
                break;
            default:
                addQuad(verts, c.pos, Vec3(1, 0, 0), worldUp, c.size, c.color);
                break;
        }
    }
    if (verts.empty()) return;

    m_indexCount = (u32)(verts.size() / 4) * 6u;
    std::vector<u32> indices(m_indexCount);
    for (u32 q = 0; q < (u32)(verts.size() / 4); ++q)
    {
        u32 base = q * 4, *p = &indices[(size_t)q * 6];
        p[0] = base;
        p[1] = base + 1;
        p[2] = base + 2;
        p[3] = base + 2;
        p[4] = base + 3;
        p[5] = base;
    }
    m_mesh.set_data(verts.data(), (u32)verts.size(), indices.data(), (u32)indices.size());
    m_mesh.upload(); // static: rebuilt only when the game calls build() again
}

void GrassSystemNode::clear()
{
    m_clumps.clear();
    m_indexCount = 0;
}
