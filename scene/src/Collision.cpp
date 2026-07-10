#include "scene/Collision.hpp"
#include <algorithm>
#include <cmath>

// ── helpers: component-wise Vec3 multiply/divide (not in scene/Math.hpp) ──
static inline Vec3 cmul(const Vec3& a, const Vec3& b)
{
    return Vec3(a.x * b.x, a.y * b.y, a.z * b.z);
}
static inline Vec3 cdiv(const Vec3& a, const Vec3& b)
{
    return Vec3(a.x / b.x, a.y / b.y, a.z / b.z);
}

// ── point-inside-triangle: same side of all 3 edges ──
static bool triContains(const Vec3& p, const Vec3& a, const Vec3& b, const Vec3& c)
{
    Vec3 n = Vec3::Cross(b - a, c - a);
    if (Vec3::Dot(Vec3::Cross(b - a, p - a), n) < 0.f) return false;
    if (Vec3::Dot(Vec3::Cross(c - b, p - b), n) < 0.f) return false;
    if (Vec3::Dot(Vec3::Cross(a - c, p - c), n) < 0.f) return false;
    return true;
}

// ── Möller-Trumbore ray-triangle intersection (returns t > 0 or -1) ──
static float rayTriT(const Triangle& tri, const Vec3& o, const Vec3& d)
{
    const float EPS = 1e-8f;
    Vec3 e1 = tri.v1 - tri.v0, e2 = tri.v2 - tri.v0;
    Vec3 pv = Vec3::Cross(d, e2);
    float det = Vec3::Dot(e1, pv);
    if (fabsf(det) < EPS) return -1.f;
    float inv = 1.f / det;
    Vec3 tv = o - tri.v0;
    float u = Vec3::Dot(tv, pv) * inv;
    if (u < 0.f || u > 1.f) return -1.f;
    Vec3 qv = Vec3::Cross(tv, e1);
    float v = Vec3::Dot(d, qv) * inv;
    if (v < 0.f || u + v > 1.f) return -1.f;
    float t = Vec3::Dot(e2, qv) * inv;
    return (t > EPS) ? t : -1.f;
}

// ── quadratic lowest positive root ──
bool CollisionSystem::lowestRoot(float a, float b, float c, float maxR, float& root)
{
    float disc = b * b - 4.f * a * c;
    if (disc < 0.f) return false;
    float sd = sqrtf(disc);
    float r1 = (-b - sd) / (2.f * a), r2 = (-b + sd) / (2.f * a);
    if (r1 > r2) std::swap(r1, r2);
    if (r1 > 0.f && r1 < maxR) { root = r1; return true; }
    if (r2 > 0.f && r2 < maxR) { root = r2; return true; }
    return false;
}

// ── broadphase candidate gathering ──
void CollisionSystem::getCandidates(const Vec3& ePos, const Vec3& eRadius,
                                    std::vector<const Triangle*>& out) const
{
    Vec3 wPos = cmul(ePos, eRadius);
    float r = Max(Max(eRadius.x, eRadius.y), eRadius.z) * 2.f;
    if (m_octree)   { m_octree->querySphere(wPos, r, out); return; }
    if (m_quadtree) { m_quadtree->query(BoundingBox(wPos - Vec3(r, r, r), wPos + Vec3(r, r, r)), out); return; }
    for (const auto& t : m_triangles) out.push_back(&t);
}

// ── triangle collision test (Fauerby: ellipsoid space) ──
bool CollisionSystem::checkTriangle(CollisionPacket& packet, const Triangle& worldTri)
{
    Vec3 p1 = cdiv(worldTri.v0, packet.eRadius);
    Vec3 p2 = cdiv(worldTri.v1, packet.eRadius);
    Vec3 p3 = cdiv(worldTri.v2, packet.eRadius);

    Vec3 planeNormal = Vec3::Cross(p2 - p1, p3 - p1);
    float l2 = planeNormal.length_squared();
    if (l2 < 1e-12f) return false;
    planeNormal = planeNormal * (1.f / sqrtf(l2));

    float normalDotVel = Vec3::Dot(planeNormal, packet.eVelocity);
    if (fabsf(normalDotVel) < 1e-6f) return false;

    float signedDist = Vec3::Dot(planeNormal, packet.ePosition - p1);
    float nDotNV = Vec3::Dot(planeNormal, packet.eNormalizedVelocity);

    float t0, t1;
    bool embedded = false;
    if (fabsf(nDotNV) < 1e-6f)
    {
        if (fabsf(signedDist) >= 1.f) return false;
        embedded = true; t0 = 0.f; t1 = 1.f;
    }
    else
    {
        float nvi = 1.f / nDotNV;
        t0 = (-1.f - signedDist) * nvi;
        t1 = (1.f - signedDist) * nvi;
        if (t0 > t1) std::swap(t0, t1);
        if (t0 > 1.f || t1 < 0.f) return false;
        t0 = Clamp(t0, 0.f, 1.f); t1 = Clamp(t1, 0.f, 1.f);
    }

    Vec3 colPoint(0, 0, 0);
    bool found = false;
    float t = 1.f;

    if (!embedded)
    {
        Vec3 planePoint = packet.ePosition - planeNormal + packet.eVelocity * t0;
        if (triContains(planePoint, p1, p2, p3)) { found = true; t = t0; colPoint = planePoint; }
    }

    if (!found)
    {
        const Vec3& vel = packet.eVelocity, &base = packet.ePosition;
        float velLen2 = vel.length_squared();
        float a, b, c, nt;

        // vertices
        a = velLen2; b = 2.f * Vec3::Dot(vel, base - p1); c = (p1 - base).length_squared() - 1.f;
        if (lowestRoot(a, b, c, t, nt)) { t = nt; found = true; colPoint = p1; }
        b = 2.f * Vec3::Dot(vel, base - p2); c = (p2 - base).length_squared() - 1.f;
        if (lowestRoot(a, b, c, t, nt)) { t = nt; found = true; colPoint = p2; }
        b = 2.f * Vec3::Dot(vel, base - p3); c = (p3 - base).length_squared() - 1.f;
        if (lowestRoot(a, b, c, t, nt)) { t = nt; found = true; colPoint = p3; }

        // edges
        auto testEdge = [&](const Vec3& ea, const Vec3& eb) {
            Vec3 edge = eb - ea, btv = ea - base;
            float eLen2 = edge.length_squared();
            float eDotVel = Vec3::Dot(edge, vel), eDotBtv = Vec3::Dot(edge, btv);
            float ea2 = eLen2 * -velLen2 + eDotVel * eDotVel;
            float eb2 = eLen2 * (2.f * Vec3::Dot(vel, btv)) - 2.f * eDotVel * eDotBtv;
            float ec2 = eLen2 * (1.f - btv.length_squared()) + eDotBtv * eDotBtv;
            if (lowestRoot(ea2, eb2, ec2, t, nt))
            {
                float f = (eDotVel * nt - eDotBtv) / eLen2;
                if (f >= 0.f && f <= 1.f) { t = nt; found = true; colPoint = ea + edge * f; }
            }
        };
        testEdge(p1, p2); testEdge(p2, p3); testEdge(p3, p1);
    }

    if (found && t >= 0.f && t <= packet.nearestDistance)
    {
        packet.nearestDistance = t;
        packet.intersectionPoint = colPoint;
        packet.foundCollision = true;
        packet.intersectionTri = &worldTri;
        return true;
    }
    return false;
}

Vec3 CollisionSystem::collideWithWorld(int depth, CollisionPacket& packet, const Vec3& pos,
                                        const Vec3& vel)
{
    if (depth > packet.maxRecursionDepth) return pos;
    packet.eVelocity = vel;
    packet.eNormalizedVelocity = vel.length_squared() > 1e-10f ? vel.normalized() : vel;
    packet.ePosition = pos;
    packet.foundCollision = false;
    packet.nearestDistance = std::numeric_limits<float>::max();

    std::vector<const Triangle*> candidates;
    getCandidates(pos, packet.eRadius, candidates);
    for (const auto* tri : candidates) checkTriangle(packet, *tri);

    if (!packet.foundCollision) return pos + vel;

    float vcd = packet.slidingSpeed;
    Vec3 newPos = pos;
    if (packet.nearestDistance >= vcd)
    {
        Vec3 vn = vel.length_squared() > 1e-10f ? vel.normalized() : vel;
        newPos = pos + vn * (packet.nearestDistance - vcd);
        packet.intersectionPoint -= vn * vcd;
    }

    Vec3 slideNormal = (newPos - packet.intersectionPoint).length_squared() > 1e-10f
                           ? (newPos - packet.intersectionPoint).normalized()
                           : packet.eNormalizedVelocity;
    Vec3 dest = pos + vel;
    float distP = Vec3::Dot(dest - packet.intersectionPoint, slideNormal);
    Vec3 newDest = dest - slideNormal * distP;
    Vec3 newVel = newDest - packet.intersectionPoint;
    if (newVel.length() < vcd) return newPos;
    return collideWithWorld(depth + 1, packet, newPos, newVel);
}

// ── public API ──

Vec3 CollisionSystem::collideAndSlide(const Vec3& position, const Vec3& velocity,
                                       const Vec3& radius, const Vec3& gravity,
                                       bool& outGrounded)
{
    CollisionPacket packet;
    packet.eRadius = radius;
    packet.R3Position = position; packet.R3Velocity = velocity;
    Vec3 ePos = cdiv(position, radius), eVel = cdiv(velocity, radius);
    Vec3 finalEPos = collideWithWorld(0, packet, ePos, eVel);

    outGrounded = false;
    if (gravity.length_squared() > 1e-10f)
    {
        packet.R3Position = cmul(finalEPos, radius); packet.R3Velocity = gravity;
        Vec3 eGrav = cdiv(gravity, radius);
        Vec3 gravPos = collideWithWorld(0, packet, finalEPos, eGrav);
        float gravDist = (gravPos - finalEPos).length() * radius.length();
        outGrounded = gravDist < packet.slidingSpeed * 2.f;
        finalEPos = gravPos;
    }
    return cmul(finalEPos, radius);
}

Vec3 CollisionSystem::collideAndSlide(const Vec3& p, const Vec3& v, const Vec3& r)
{
    bool g; return collideAndSlide(p, v, r, Vec3(0, 0, 0), g);
}

Vec3 CollisionSystem::sphereSlide(const Vec3& p, const Vec3& v, float r, const Vec3& g, bool& out)
{
    return collideAndSlide(p, v, Vec3(r, r, r), g, out);
}

Vec3 CollisionSystem::sphereSlide(const Vec3& p, const Vec3& v, float r)
{
    bool g; return sphereSlide(p, v, r, Vec3(0, 0, 0), g);
}

bool CollisionSystem::rayCast(const Ray& ray, float maxDist, CollisionInfo& out) const
{
    out = {};
    out.distance = maxDist;
    for (const auto& tri : m_triangles)
    {
        float t = rayTriT(tri, ray.origin, ray.direction);
        if (t > 0.f && t < out.distance)
        {
            out.hit = true; out.distance = t; out.point = ray.pointAt(t);
            out.normal = tri.normal(); out.triangle = &tri;
        }
    }
    return out.hit;
}

bool CollisionSystem::pointInside(const Vec3& point) const
{
    Ray ray(point, Vec3(1.f, 0.707f, 0.707f).normalized());
    int hits = 0;
    for (const auto& tri : m_triangles)
        if (rayTriT(tri, ray.origin, ray.direction) > 0.f) ++hits;
    return (hits & 1) == 1;
}
