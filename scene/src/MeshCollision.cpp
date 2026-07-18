#include "scene/MeshCollision.hpp"
#include "scene/Mesh.hpp"
#include "scene/Node3D.hpp"
#include <algorithm>
#include <cmath>
#include <unordered_map>

// ══════════════════════════════════════════════════════════════════════
// MeshCollider: box-tree build (top-down binary AABB, up to
// kMaxLeafTriangles triangles per leaf — leaves store indices into the
// Mesh's own index buffer, triIndex*3+0..2, never a copy of the triangle)
// + narrow phase.
// ══════════════════════════════════════════════════════════════════════

Triangle MeshCollider::triangle_at(u32 triIndex) const
{
    const std::vector<u32>& idx = m_mesh->indices();
    const std::vector<MeshVertex>& verts = m_mesh->vertices();
    return Triangle(verts[idx[triIndex * 3 + 0]].position, verts[idx[triIndex * 3 + 1]].position,
                    verts[idx[triIndex * 3 + 2]].position);
}

int MeshCollider::build_range(std::vector<u32>& triIndices, size_t begin, size_t end)
{
    Node node;

    // tight bounds over this range's triangles, plus centroid average (used
    // to pick the split axis/value below)
    Vec3 mn(1e30f, 1e30f, 1e30f), mx(-1e30f, -1e30f, -1e30f), centroidSum(0.f, 0.f, 0.f);
    for (size_t i = begin; i < end; ++i)
    {
        Triangle t = triangle_at(triIndices[i]);
        BoundingBox b = t.bounds();
        mn = mn.Min(b.min);
        mx = mx.Max(b.max);
        centroidSum = centroidSum + t.center();
    }
    node.bounds = BoundingBox(mn, mx);

    size_t count = end - begin;
    if (count <= kMaxLeafTriangles)
    {
        node.leafStart = (u32)m_leafTriangles.size();
        node.leafCount = (u32)count;
        m_leafTriangles.insert(m_leafTriangles.end(), triIndices.begin() + (long)begin,
                               triIndices.begin() + (long)end);
        m_nodes.push_back(node);
        return (int)m_nodes.size() - 1;
    }

    Vec3 centroidMean = centroidSum * (1.f / (float)count);

    // split axis = the one where triangle centroids deviate most from the
    // mean (widest spread), split value = that mean itself
    Vec3 deviation(0.f, 0.f, 0.f);
    for (size_t i = begin; i < end; ++i)
    {
        Vec3 c = triangle_at(triIndices[i]).center();
        deviation.x += fabsf(c.x - centroidMean.x);
        deviation.y += fabsf(c.y - centroidMean.y);
        deviation.z += fabsf(c.z - centroidMean.z);
    }
    int axis = 0;
    if (deviation.y > deviation.x && deviation.y >= deviation.z) axis = 1;
    else if (deviation.z > deviation.x && deviation.z >= deviation.y) axis = 2;
    float splitValue = (axis == 0) ? centroidMean.x : (axis == 1) ? centroidMean.y : centroidMean.z;

    auto centroidAxis = [&](u32 triIndex) {
        Vec3 c = triangle_at(triIndex).center();
        return axis == 0 ? c.x : axis == 1 ? c.y : c.z;
    };

    size_t mid = (size_t)(std::partition(triIndices.begin() + (long)begin,
                                         triIndices.begin() + (long)end,
                                         [&](u32 t) { return centroidAxis(t) < splitValue; }) -
                         triIndices.begin());

    // degenerate split (every triangle landed on one side, e.g. all
    // centroids coincide) — fall back to a plain half split so recursion
    // always makes progress instead of looping forever on the same range
    if (mid == begin || mid == end) mid = begin + count / 2;

    m_nodes.push_back(node); // reserve this node's slot before recursing
    int selfIndex = (int)m_nodes.size() - 1;
    int leftIndex = build_range(triIndices, begin, mid);
    int rightIndex = build_range(triIndices, mid, end);
    m_nodes[(size_t)selfIndex].left = leftIndex;
    m_nodes[(size_t)selfIndex].right = rightIndex;
    return selfIndex;
}

void MeshCollider::build(const Mesh* mesh)
{
    m_mesh = mesh;
    m_nodes.clear();
    u32 triCount = (u32)(mesh->indices().size() / 3);
    if (triCount == 0) return;

    m_nodes.reserve(triCount * 2 / kMaxLeafTriangles + 1);
    m_leafTriangles.reserve(triCount);
    std::vector<u32> triIndices(triCount);
    for (u32 i = 0; i < triCount; ++i) triIndices[i] = i;
    build_range(triIndices, 0, triIndices.size());
}

MeshCollider* MeshCollider::get_or_create(Mesh* mesh)
{
    static std::unordered_map<Mesh*, MeshCollider*> cache;
    auto it = cache.find(mesh);
    if (it != cache.end()) return it->second;

    MeshCollider* collider = new MeshCollider();
    collider->build(mesh);
    cache[mesh] = collider;
    return collider;
}

// finds triangle t's two edge/plane crossing points against the plane
// (n,d) of the other triangle. False if t doesn't straddle that plane.
bool MeshCollider::edge_plane_crossings(const Triangle& t, const Vec3& n, float d, Vec3& outP0,
                                        Vec3& outP1)
{
    float dist[3] = {Vec3::Dot(n, t.v0) + d, Vec3::Dot(n, t.v1) + d, Vec3::Dot(n, t.v2) + d};
    const Vec3 v[3] = {t.v0, t.v1, t.v2};

    const float eps = 1e-6f;
    int sign[3];
    for (int i = 0; i < 3; ++i) sign[i] = (dist[i] > eps) ? 1 : (dist[i] < -eps ? -1 : 0);

    if (sign[0] == sign[1] && sign[1] == sign[2]) return false; // doesn't straddle

    // find the vertex alone on its side (the other two share a sign, or
    // are on the plane) — that vertex's two edges are the ones crossing
    int lone = -1;
    for (int i = 0; i < 3; ++i)
    {
        int a = (i + 1) % 3, b = (i + 2) % 3;
        if (sign[i] != 0 && sign[i] != sign[a] && sign[i] != sign[b])
        {
            lone = i;
            break;
        }
    }
    if (lone < 0) lone = 0; // degenerate (a vertex exactly on the plane) — pick any, good enough

    int other0 = (lone + 1) % 3, other1 = (lone + 2) % 3;
    float dl = dist[lone];
    float t0 = dl - dist[other0], t1 = dl - dist[other1];
    float u0 = fabsf(t0) > eps ? dl / t0 : 0.f;
    float u1 = fabsf(t1) > eps ? dl / t1 : 0.f;
    outP0 = v[lone] + (v[other0] - v[lone]) * u0;
    outP1 = v[lone] + (v[other1] - v[lone]) * u1;
    return true;
}

// boolean + contact test between two triangles already in the same
// (world) space. On a hit, fills point/normal/depth.
bool MeshCollider::triangle_triangle(const Triangle& a, const Triangle& b, ContactInfo& out)
{
    Vec3 na = a.normal();
    float da = -Vec3::Dot(na, a.v0);
    Vec3 nb = b.normal();
    float db = -Vec3::Dot(nb, b.v0);

    Vec3 pa0, pa1, pb0, pb1;
    // both triangles must actually straddle the other's plane for a real
    // intersection (mirrors the classic algorithm's two early-out
    // rejection tests, done here as one combined check since we need the
    // crossing points either way)
    if (!edge_plane_crossings(b, na, da, pb0, pb1)) return false;
    if (!edge_plane_crossings(a, nb, db, pa0, pa1)) return false;

    // both crossing segments lie on the line where the two planes meet
    // (direction D = cross(na,nb)); project onto D to get 1D intervals and
    // test/compute their overlap
    Vec3 D = Vec3::Cross(na, nb);
    float lenSq = D.length_squared();
    if (lenSq < 1e-12f) return false; // near-parallel triangles — not handled (rare for game props)

    float ta0 = Vec3::Dot(D, pa0), ta1 = Vec3::Dot(D, pa1);
    float tb0 = Vec3::Dot(D, pb0), tb1 = Vec3::Dot(D, pb1);
    if (ta0 > ta1)
    {
        std::swap(ta0, ta1);
        std::swap(pa0, pa1);
    }
    if (tb0 > tb1)
    {
        std::swap(tb0, tb1);
        std::swap(pb0, pb1);
    }

    if (ta1 < tb0 || tb1 < ta0) return false; // intervals don't overlap — no intersection

    // overlap sub-segment: the two middle points out of the four, by t
    Vec3 lowPoint = (ta0 > tb0) ? pa0 : pb0;
    Vec3 highPoint = (ta1 < tb1) ? pa1 : pb1;

    out.hit = true;
    out.point = (lowPoint + highPoint) * 0.5f;
    out.normal = na;
    // depth: how far B's vertices push behind A's plane (clamped, see
    // ContactInfo comment — an approximation, not true penetration depth)
    float depth = 0.f;
    depth = std::max(depth, -(Vec3::Dot(na, b.v0) + da));
    depth = std::max(depth, -(Vec3::Dot(na, b.v1) + da));
    depth = std::max(depth, -(Vec3::Dot(na, b.v2) + da));
    out.depth = depth;
    return true;
}

// ══════════════════════════════════════════════════════════════════════
// broad phase: iterative worklist over both box-trees, world-space AABB
// envelope test (BoundingBox::TransformBoundingBox) at each node pair,
// narrow-phase test at leaf×leaf pairs, first hit stops the search.
// ══════════════════════════════════════════════════════════════════════

bool MeshCollider::test(const Mat4& worldSelf, const MeshCollider& other, const Mat4& worldOther,
                        ContactInfo& out) const
{
    if (m_nodes.empty() || other.m_nodes.empty()) return false;

    struct Pair
    {
        int a, b;
    };
    std::vector<Pair> stack;
    // root is always index 0: build_range() pushes a node's own slot
    // before recursing into its children, so the top-level call's node
    // (the root) is always the first one in m_nodes, not the last.
    stack.push_back(Pair{0, 0});

    // a leaf's triangles get world-transformed at most once per test()
    // call, cached here — without this, a leaf that matches several
    // leaves on the other side (the normal case once the broad phase
    // narrows down) would redo the same matrix multiplies every time
    std::unordered_map<int, std::vector<Triangle>> cacheSelf, cacheOther;
    auto transformedLeaf = [](const MeshCollider& mc, const Node& n, const Mat4& world,
                              std::unordered_map<int, std::vector<Triangle>>& cache,
                              int nodeIndex) -> const std::vector<Triangle>&
    {
        auto it = cache.find(nodeIndex);
        if (it != cache.end()) return it->second;
        std::vector<Triangle> tris;
        tris.reserve(n.leafCount);
        for (u32 i = 0; i < n.leafCount; ++i)
        {
            Triangle t = mc.triangle_at(mc.m_leafTriangles[n.leafStart + i]);
            t.v0 = Mat4::Transform(world, t.v0);
            t.v1 = Mat4::Transform(world, t.v1);
            t.v2 = Mat4::Transform(world, t.v2);
            tris.push_back(t);
        }
        return cache.emplace(nodeIndex, std::move(tris)).first->second;
    };

    while (!stack.empty())
    {
        Pair p = stack.back();
        stack.pop_back();
        const Node& na = m_nodes[(size_t)p.a];
        const Node& nb = other.m_nodes[(size_t)p.b];

        BoundingBox worldBoxA = BoundingBox::TransformBoundingBox(na.bounds, worldSelf);
        BoundingBox worldBoxB = BoundingBox::TransformBoundingBox(nb.bounds, worldOther);
        if (!worldBoxA.intersects(worldBoxB)) continue;

        if (na.is_leaf() && nb.is_leaf())
        {
            const std::vector<Triangle>& trisA = transformedLeaf(*this, na, worldSelf, cacheSelf, p.a);
            const std::vector<Triangle>& trisB =
                transformedLeaf(other, nb, worldOther, cacheOther, p.b);
            for (u32 i = 0; i < na.leafCount; ++i)
            {
                for (u32 j = 0; j < nb.leafCount; ++j)
                {
                    if (triangle_triangle(trisA[i], trisB[j], out))
                    {
                        out.triIndexA = m_leafTriangles[na.leafStart + i];
                        out.triIndexB = other.m_leafTriangles[nb.leafStart + j];
                        return true;
                    }
                }
            }
            continue;
        }

        if (!na.is_leaf() && !nb.is_leaf())
        {
            // both inner: descend only the larger side this round (a
            // standard heuristic to balance work) — the other side gets
            // its turn once this one bottoms out
            float volA = (na.bounds.max.x - na.bounds.min.x) * (na.bounds.max.y - na.bounds.min.y) *
                        (na.bounds.max.z - na.bounds.min.z);
            float volB = (nb.bounds.max.x - nb.bounds.min.x) * (nb.bounds.max.y - nb.bounds.min.y) *
                        (nb.bounds.max.z - nb.bounds.min.z);
            if (volA >= volB)
            {
                stack.push_back(Pair{na.left, p.b});
                stack.push_back(Pair{na.right, p.b});
            }
            else
            {
                stack.push_back(Pair{p.a, nb.left});
                stack.push_back(Pair{p.a, nb.right});
            }
        }
        else if (!na.is_leaf())
        {
            stack.push_back(Pair{na.left, p.b});
            stack.push_back(Pair{na.right, p.b});
        }
        else // !nb.is_leaf()
        {
            stack.push_back(Pair{p.a, nb.left});
            stack.push_back(Pair{p.a, nb.right});
        }
    }
    return false;
}

// ══════════════════════════════════════════════════════════════════════
// registry
// ══════════════════════════════════════════════════════════════════════

int CollisionRegistry::add(Mesh* mesh, Node3D* node)
{
    Entry e;
    e.collider = MeshCollider::get_or_create(mesh);
    e.node = node;
    e.alive = true;

    if (!m_freeList.empty())
    {
        int handle = m_freeList.back();
        m_freeList.pop_back();
        m_entries[(size_t)handle] = e;
        return handle;
    }
    m_entries.push_back(e);
    return (int)m_entries.size() - 1;
}

void CollisionRegistry::remove(int handle)
{
    if (handle < 0 || (size_t)handle >= m_entries.size() || !m_entries[(size_t)handle].alive)
        return;
    m_entries[(size_t)handle] = Entry();
    m_freeList.push_back(handle);
}

Node3D* CollisionRegistry::node_of(int handle) const
{
    if (handle < 0 || (size_t)handle >= m_entries.size() || !m_entries[(size_t)handle].alive)
        return nullptr;
    return m_entries[(size_t)handle].node;
}

bool CollisionRegistry::test(int handleA, int handleB, ContactInfo& out) const
{
    if (handleA < 0 || (size_t)handleA >= m_entries.size() || !m_entries[(size_t)handleA].alive)
        return false;
    if (handleB < 0 || (size_t)handleB >= m_entries.size() || !m_entries[(size_t)handleB].alive)
        return false;
    const Entry& ea = m_entries[(size_t)handleA];
    const Entry& eb = m_entries[(size_t)handleB];
    return ea.collider->test(ea.node->get_world_matrix(), *eb.collider, eb.node->get_world_matrix(),
                             out);
}

bool CollisionRegistry::test_and_notify(int handleA, int handleB, CollisionCallback cb,
                                        void* userData) const
{
    ContactInfo info;
    if (!test(handleA, handleB, info)) return false;
    if (cb) cb(m_entries[(size_t)handleA].node, m_entries[(size_t)handleB].node, info, userData);
    return true;
}
