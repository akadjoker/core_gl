#pragma once

#include "scene/Math.hpp" // Triangle, Ray, Vec3
#include "scene/Tree.hpp" // Octree, Quadtree
#include <limits>
#include <vector>

// ── Collision info (result of a raycast) ──

struct CollisionInfo
{
    bool           hit = false;
    Vec3           point{0, 0, 0};
    Vec3           normal{0, 0, 0};
    float          distance = std::numeric_limits<float>::max();
    const Triangle* triangle = nullptr;
};

// ── Ellipsoid sliding state (Fauerby algorithm) ──

struct CollisionPacket
{
    Vec3  eRadius{1, 1, 1};
    Vec3  R3Position, R3Velocity;
    Vec3  ePosition, eVelocity, eNormalizedVelocity;
    bool  foundCollision = false;
    float nearestDistance = std::numeric_limits<float>::max();
    Vec3  intersectionPoint{0, 0, 0};
    const Triangle* intersectionTri = nullptr;
    float slidingSpeed = 0.005f;
    int   maxRecursionDepth = 5;
};

// ── Triangle-soup collision system ──
//
// Provides: collideAndSlide (Fauerby ellipsoid), sphereSlide (convenience),
// rayCast (Möller-Trumbore), and pointInside (odd-parity ray test).
// An optional Octree/Quadtree accelerates broadphase queries.
//
 

class CollisionSystem
{
public:
    CollisionSystem() = default;

    void addTriangle(const Triangle& tri) { m_triangles.push_back(tri); }
    void addTriangles(const std::vector<Triangle>& t)
    {
        m_triangles.insert(m_triangles.end(), t.begin(), t.end());
    }
    void clear() { m_triangles.clear(); }
    int  triangleCount() const { return (int)m_triangles.size(); }
    const std::vector<Triangle>& getTriangles() const { return m_triangles; }

    void setOctree(Octree* t)   { m_octree = t; m_quadtree = nullptr; }
    void setQuadtree(Quadtree* t) { m_quadtree = t; m_octree = nullptr; }

    // Ellipsoid-space epsilon kept between the body and any surface it
    // rests against (CollisionPacket::slidingSpeed default is 0.005 — tiny,
    // easy to get pinned in a corner with). Per-instance, not global: only
    // callers that call this see a different value, everyone else keeps
    // the 0.005 default.
    void setSlidingSpeed(float s) { m_slidingSpeed = s; }
    float slidingSpeed() const { return m_slidingSpeed; }

    // ── Slide ──
    Vec3 collideAndSlide(const Vec3& position, const Vec3& velocity, const Vec3& radius,
                         const Vec3& gravity, bool& outGrounded);
    Vec3 collideAndSlide(const Vec3& position, const Vec3& velocity, const Vec3& radius);
    Vec3 sphereSlide(const Vec3& position, const Vec3& velocity, float radius,
                     const Vec3& gravity, bool& outGrounded);
    Vec3 sphereSlide(const Vec3& position, const Vec3& velocity, float radius);

    // ── Queries ──
    bool rayCast(const Ray& ray, float maxDist, CollisionInfo& out) const;
    bool pointInside(const Vec3& point) const;

private:
    std::vector<Triangle> m_triangles;
    Octree*   m_octree = nullptr;
    Quadtree* m_quadtree = nullptr;
    float m_slidingSpeed = 0.005f;

    void getCandidates(const Vec3& ePos, const Vec3& eRadius,
                       std::vector<const Triangle*>& out) const;
    bool checkTriangle(CollisionPacket& packet, const Triangle& tri);
    static bool lowestRoot(float a, float b, float c, float maxR, float& root);
    Vec3 collideWithWorld(int depth, CollisionPacket& packet, const Vec3& pos, const Vec3& vel);
};
