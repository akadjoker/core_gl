#pragma once

#include "scene/Math.hpp" // Triangle, BoundingBox, Ray, Frustum
#include <vector>

// Spatial trees for triangle-based broadphase collision/picking (NOT render
// culling).  Quadtree (XZ split, full Y) and Octree (full 3D split).
// Ported from tmp/core/Tree — pure algorithm, zero dependencies beyond Math.

// ── Quadtree: splits on XZ, keeps full Y extent ──

struct QuadtreeNode
{
    BoundingBox            bounds;
    QuadtreeNode*          children[4] = {nullptr, nullptr, nullptr, nullptr};
    std::vector<Triangle>  triangles;
    int                    depth = 0;
    bool                   isLeaf = true;

    explicit QuadtreeNode(const BoundingBox& b, int d) : bounds(b), depth(d) {}
    ~QuadtreeNode();
    void split();
    bool overlapsTriangle(const Triangle& tri) const { return bounds.intersects(tri.bounds()); }
};

class Quadtree
{
public:
    Quadtree(const BoundingBox& bounds, int maxDepth = 8, int maxTriPerNode = 16);
    ~Quadtree();

    void clear();
    void build(const std::vector<Triangle>& tris);
    void insert(const Triangle& tri);
    void query(const BoundingBox& region, std::vector<const Triangle*>& out) const;
    void query(const Vec3& point, float radius, std::vector<const Triangle*>& out) const;
    void queryRay(const Ray& ray, float maxDist, std::vector<const Triangle*>& out) const;
    int  totalTriangles() const { return m_totalTris; }

private:
    QuadtreeNode* m_root = nullptr;
    int  m_maxDepth, m_maxTriPerNode, m_totalTris = 0;

    void insertRecursive(QuadtreeNode* node, const Triangle& tri);
    void queryAABB(const QuadtreeNode* node, const BoundingBox& region,
                   std::vector<const Triangle*>& out) const;
    void queryRayRec(const QuadtreeNode* node, const Ray& ray, float maxDist,
                     std::vector<const Triangle*>& out) const;
};

// ── Octree: full 3D split ──

struct OctreeNode
{
    BoundingBox            bounds;
    OctreeNode*            children[8] = {};
    std::vector<Triangle>  triangles;
    int                    depth = 0;
    bool                   isLeaf = true;

    explicit OctreeNode(const BoundingBox& b, int d) : bounds(b), depth(d) {}
    ~OctreeNode();
    void split();
    bool overlapsTriangle(const Triangle& tri) const { return bounds.intersects(tri.bounds()); }
};

class Octree
{
public:
    Octree(const BoundingBox& bounds, int maxDepth = 8, int maxTriPerNode = 16);
    ~Octree();

    void clear();
    void build(const std::vector<Triangle>& tris);
    void insert(const Triangle& tri);
    void query(const BoundingBox& region, std::vector<const Triangle*>& out) const;
    void querySphere(const Vec3& center, float radius, std::vector<const Triangle*>& out) const;
    void queryRay(const Ray& ray, float maxDist, std::vector<const Triangle*>& out) const;
    void queryFrustum(const Frustum& frustum, std::vector<const Triangle*>& out) const;
    int  totalTriangles() const { return m_totalTris; }

private:
    OctreeNode* m_root = nullptr;
    int  m_maxDepth, m_maxTriPerNode, m_totalTris = 0;

    void insertRecursive(OctreeNode* node, const Triangle& tri);
    void queryAABB(const OctreeNode* node, const BoundingBox& region,
                   std::vector<const Triangle*>& out) const;
    void querySphereRec(const OctreeNode* node, const Vec3& c, float r,
                        std::vector<const Triangle*>& out) const;
    void queryRayRec(const OctreeNode* node, const Ray& ray, float maxDist,
                     std::vector<const Triangle*>& out) const;
    void queryFrustumRec(const OctreeNode* node, const Frustum& frustum,
                         std::vector<const Triangle*>& out) const;
};
