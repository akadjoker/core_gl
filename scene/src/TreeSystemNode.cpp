#include "scene/TreeSystemNode.hpp"
#include <cmath>
#include <random>

TreeSystemNode::TreeSystemNode(const std::string& name, int maxTrees)
    : Node3D(name), m_maxTrees(maxTrees)
{
    m_type = NT_TREESYSTEM;
}

void TreeSystemNode::addTree(const Vec3& pos, const Vec2& size, int atlasCol, int atlasRow,
                             const Vec4& color)
{
    if ((int)m_trees.size() >= m_maxTrees) return;
    atlasCol = atlasCol < 0 ? 0 : (atlasCol >= m_atlasCols ? m_atlasCols - 1 : atlasCol);
    atlasRow = atlasRow < 0 ? 0 : (atlasRow >= m_atlasRows ? m_atlasRows - 1 : atlasRow);
    m_trees.push_back({pos, size, color, atlasCol, atlasRow});
}

void TreeSystemNode::fillArea(const Vec3& center, float w, float d, int count, float mn, float mx,
                              unsigned seed)
{
    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> u(0.f, 1.f);
    for (int i = 0; i < count; ++i)
    {
        Vec3 p = center + Vec3((u(rng) - 0.5f) * w, 0.f, (u(rng) - 0.5f) * d);
        float s = mn + u(rng) * (mx - mn);
        int col = (int)(u(rng) * (float)m_atlasCols);
        int row = (int)(u(rng) * (float)m_atlasRows);
        addTree(p, Vec2(s * 0.7f, s), col, row);
    }
}

void TreeSystemNode::addQuad(std::vector<MeshVertex>& v, const Vec3& center, const Vec3& right,
                             const Vec3& up, const Vec2& size, const Vec4& color, const Vec2& uvMin,
                             const Vec2& uvMax)
{
    float hw = size.x * 0.5f, h = size.y;
    // wind sway needs "how far up this vertex is" (0 base, 1 tip), but with
    // an atlas uv.y is a real texture coordinate inside whatever cell this
    // tree picked, not a 0..1 local height — so unlike GrassSystemNode (no
    // atlas, uv.y doubles as both) this rides the otherwise-unused normal
    // slot instead: normal.x = height fraction, read by kTreeVS.
    auto push = [&](const Vec3& pos, const Vec2& uv, float heightFrac)
    {
        MeshVertex mv;
        mv.position = pos;
        mv.normal = Vec3(heightFrac, 0.f, 0.f);
        mv.tangent = color; // color rides the unused tangent slot (see GrassSystemNode)
        mv.uv = uv;
        v.push_back(mv);
    };
    push(center - right * hw, Vec2(uvMin.x, uvMax.y), 0.f);
    push(center + right * hw, Vec2(uvMax.x, uvMax.y), 0.f);
    push(center + right * hw + up * h, Vec2(uvMax.x, uvMin.y), 1.f);
    push(center - right * hw + up * h, Vec2(uvMin.x, uvMin.y), 1.f);
}

void TreeSystemNode::build()
{
    std::vector<MeshVertex> verts;
    const Vec3 worldUp(0, 1, 0);
    const float cellU = 1.f / (float)m_atlasCols, cellV = 1.f / (float)m_atlasRows;
    for (const Tree& t : m_trees)
    {
        Vec2 uvMin(t.atlasCol * cellU, t.atlasRow * cellV);
        Vec2 uvMax(uvMin.x + cellU, uvMin.y + cellV);
        // crossed billboard: two quads at right angles through the trunk axis
        addQuad(verts, t.pos, Vec3(1, 0, 0), worldUp, t.size, t.color, uvMin, uvMax);
        addQuad(verts, t.pos, Vec3(0, 0, 1), worldUp, t.size, t.color, uvMin, uvMax);
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

void TreeSystemNode::clear()
{
    m_trees.clear();
    m_indexCount = 0;
}
