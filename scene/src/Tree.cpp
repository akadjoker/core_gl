#include "scene/Tree.hpp"
#include <utility>

// ── QuadtreeNode ──

QuadtreeNode::~QuadtreeNode()
{
    for (int i = 0; i < 4; ++i) delete children[i];
}

void QuadtreeNode::split()
{
    if (!isLeaf) return;
    isLeaf = false;
    Vec3 c = bounds.center();
    float y0 = bounds.min.y, y1 = bounds.max.y;

    children[0] = new QuadtreeNode(
        BoundingBox(Vec3(bounds.min.x, y0, bounds.min.z), Vec3(c.x, y1, c.z)), depth + 1);
    children[1] = new QuadtreeNode(
        BoundingBox(Vec3(c.x, y0, bounds.min.z), Vec3(bounds.max.x, y1, c.z)), depth + 1);
    children[2] = new QuadtreeNode(
        BoundingBox(Vec3(bounds.min.x, y0, c.z), Vec3(c.x, y1, bounds.max.z)), depth + 1);
    children[3] = new QuadtreeNode(
        BoundingBox(Vec3(c.x, y0, c.z), Vec3(bounds.max.x, y1, bounds.max.z)), depth + 1);
}

// ── Quadtree ──

Quadtree::Quadtree(const BoundingBox& b, int maxD, int maxTri)
    : m_maxDepth(maxD), m_maxTriPerNode(maxTri)
{
    m_root = new QuadtreeNode(b, 0);
}

Quadtree::~Quadtree() { delete m_root; }

void Quadtree::clear()
{
    BoundingBox b = m_root->bounds;
    delete m_root;
    m_root = new QuadtreeNode(b, 0);
    m_totalTris = 0;
}

void Quadtree::build(const std::vector<Triangle>& tris)
{
    clear();
    for (const auto& t : tris) insert(t);
}

void Quadtree::insert(const Triangle& tri)
{
    insertRecursive(m_root, tri);
    ++m_totalTris;
}

void Quadtree::insertRecursive(QuadtreeNode* node, const Triangle& tri)
{
    if (!node->overlapsTriangle(tri)) return;
    if (node->isLeaf)
    {
        node->triangles.push_back(tri);
        if ((int)node->triangles.size() > m_maxTriPerNode && node->depth < m_maxDepth)
        {
            node->split();
            std::vector<Triangle> ex = std::move(node->triangles);
            node->triangles.clear();
            for (const auto& t : ex)
                for (int i = 0; i < 4; ++i)
                    if (node->children[i]->overlapsTriangle(t))
                        node->children[i]->triangles.push_back(t);
        }
    }
    else
    {
        for (int i = 0; i < 4; ++i)
            insertRecursive(node->children[i], tri);
    }
}

void Quadtree::query(const BoundingBox& region, std::vector<const Triangle*>& out) const
{
    queryAABB(m_root, region, out);
}

void Quadtree::queryAABB(const QuadtreeNode* node, const BoundingBox& region,
                         std::vector<const Triangle*>& out) const
{
    if (!node || !node->bounds.intersects(region)) return;
    for (const auto& t : node->triangles)
        if (region.intersects(t.bounds())) out.push_back(&t);
    if (!node->isLeaf)
        for (int i = 0; i < 4; ++i) queryAABB(node->children[i], region, out);
}

void Quadtree::query(const Vec3& p, float r, std::vector<const Triangle*>& out) const
{
    query(BoundingBox(p - Vec3(r, r, r), p + Vec3(r, r, r)), out);
}

void Quadtree::queryRay(const Ray& ray, float maxDist, std::vector<const Triangle*>& out) const
{
    queryRayRec(m_root, ray, maxDist, out);
}

void Quadtree::queryRayRec(const QuadtreeNode* node, const Ray& ray, float maxDist,
                           std::vector<const Triangle*>& out) const
{
    if (!node) return;
    float t = node->bounds.intersects_ray(ray.origin, ray.direction);
    if (t < 0.f || t > maxDist) return;
    for (const auto& tri : node->triangles) out.push_back(&tri);
    if (!node->isLeaf)
        for (int i = 0; i < 4; ++i) queryRayRec(node->children[i], ray, maxDist, out);
}

// ── OctreeNode ──

OctreeNode::~OctreeNode()
{
    for (int i = 0; i < 8; ++i) delete children[i];
}

void OctreeNode::split()
{
    if (!isLeaf) return;
    isLeaf = false;
    Vec3 mn = bounds.min, mx = bounds.max, c = bounds.center();

    children[0] = new OctreeNode(BoundingBox(Vec3(mn.x, mn.y, mn.z), Vec3(c.x, c.y, c.z)), depth + 1);
    children[1] = new OctreeNode(BoundingBox(Vec3(c.x, mn.y, mn.z), Vec3(mx.x, c.y, c.z)), depth + 1);
    children[2] = new OctreeNode(BoundingBox(Vec3(mn.x, c.y, mn.z), Vec3(c.x, mx.y, c.z)), depth + 1);
    children[3] = new OctreeNode(BoundingBox(Vec3(c.x, c.y, mn.z), Vec3(mx.x, mx.y, c.z)), depth + 1);
    children[4] = new OctreeNode(BoundingBox(Vec3(mn.x, mn.y, c.z), Vec3(c.x, c.y, mx.z)), depth + 1);
    children[5] = new OctreeNode(BoundingBox(Vec3(c.x, mn.y, c.z), Vec3(mx.x, c.y, mx.z)), depth + 1);
    children[6] = new OctreeNode(BoundingBox(Vec3(mn.x, c.y, c.z), Vec3(c.x, mx.y, mx.z)), depth + 1);
    children[7] = new OctreeNode(BoundingBox(Vec3(c.x, c.y, c.z), Vec3(mx.x, mx.y, mx.z)), depth + 1);
}

// ── Octree ──

Octree::Octree(const BoundingBox& b, int maxD, int maxTri)
    : m_maxDepth(maxD), m_maxTriPerNode(maxTri)
{
    m_root = new OctreeNode(b, 0);
}

Octree::~Octree() { delete m_root; }

void Octree::clear()
{
    BoundingBox b = m_root->bounds;
    delete m_root;
    m_root = new OctreeNode(b, 0);
    m_totalTris = 0;
}

void Octree::build(const std::vector<Triangle>& tris)
{
    clear();
    for (const auto& t : tris) insert(t);
}

void Octree::insert(const Triangle& tri)
{
    insertRecursive(m_root, tri);
    ++m_totalTris;
}

void Octree::insertRecursive(OctreeNode* node, const Triangle& tri)
{
    if (!node->overlapsTriangle(tri)) return;
    if (node->isLeaf)
    {
        node->triangles.push_back(tri);
        if ((int)node->triangles.size() > m_maxTriPerNode && node->depth < m_maxDepth)
        {
            node->split();
            std::vector<Triangle> ex = std::move(node->triangles);
            node->triangles.clear();
            for (const auto& t : ex)
                for (int i = 0; i < 8; ++i)
                    if (node->children[i]->overlapsTriangle(t))
                        node->children[i]->triangles.push_back(t);
        }
    }
    else
    {
        for (int i = 0; i < 8; ++i)
            insertRecursive(node->children[i], tri);
    }
}

void Octree::query(const BoundingBox& region, std::vector<const Triangle*>& out) const
{
    queryAABB(m_root, region, out);
}

void Octree::queryAABB(const OctreeNode* node, const BoundingBox& region,
                       std::vector<const Triangle*>& out) const
{
    if (!node || !node->bounds.intersects(region)) return;
    for (const auto& t : node->triangles)
        if (region.intersects(t.bounds())) out.push_back(&t);
    if (!node->isLeaf)
        for (int i = 0; i < 8; ++i) queryAABB(node->children[i], region, out);
}

// sphere vs AABB (closest-point test without a Sphere struct)
static bool sphereHitsBox(const Vec3& c, float r, const BoundingBox& b)
{
    float dx = c.x - Clamp(c.x, b.min.x, b.max.x);
    float dy = c.y - Clamp(c.y, b.min.y, b.max.y);
    float dz = c.z - Clamp(c.z, b.min.z, b.max.z);
    return dx * dx + dy * dy + dz * dz <= r * r;
}

void Octree::querySphere(const Vec3& c, float r, std::vector<const Triangle*>& out) const
{
    querySphereRec(m_root, c, r, out);
}

void Octree::querySphereRec(const OctreeNode* node, const Vec3& c, float r,
                            std::vector<const Triangle*>& out) const
{
    if (!node || !sphereHitsBox(c, r, node->bounds)) return;
    for (const auto& t : node->triangles)
        if (sphereHitsBox(c, r, t.bounds())) out.push_back(&t);
    if (!node->isLeaf)
        for (int i = 0; i < 8; ++i) querySphereRec(node->children[i], c, r, out);
}

void Octree::queryRay(const Ray& ray, float maxDist, std::vector<const Triangle*>& out) const
{
    queryRayRec(m_root, ray, maxDist, out);
}

void Octree::queryRayRec(const OctreeNode* node, const Ray& ray, float maxDist,
                         std::vector<const Triangle*>& out) const
{
    if (!node) return;
    float t = node->bounds.intersects_ray(ray.origin, ray.direction);
    if (t < 0.f || t > maxDist) return;
    for (const auto& tri : node->triangles) out.push_back(&tri);
    if (!node->isLeaf)
        for (int i = 0; i < 8; ++i) queryRayRec(node->children[i], ray, maxDist, out);
}

void Octree::queryFrustum(const Frustum& frustum, std::vector<const Triangle*>& out) const
{
    queryFrustumRec(m_root, frustum, out);
}

void Octree::queryFrustumRec(const OctreeNode* node, const Frustum& frustum,
                             std::vector<const Triangle*>& out) const
{
    if (!node || !frustum.ContainsBox(node->bounds)) return;
    for (const auto& tri : node->triangles) out.push_back(&tri);
    if (!node->isLeaf)
        for (int i = 0; i < 8; ++i) queryFrustumRec(node->children[i], frustum, out);
}
