#include "scene/World.hpp"
#include "scene/Mesh.hpp"
#include "scene/Node3D.hpp"
#include <algorithm>
#include <cmath>

// ══════════════════════════════════════════════════════════════════════
// registration
// ══════════════════════════════════════════════════════════════════════

void World::add_static_sphere(Node3D* node, float radius)
{
    StaticCollider s;
    s.shape = ColliderShape::Sphere;
    s.node = node;
    s.radius = radius;
    m_statics.push_back(s);
}

void World::add_static_box(Node3D* node, const Vec3& halfExtent)
{
    StaticCollider s;
    s.shape = ColliderShape::Box;
    s.node = node;
    s.halfExtent = halfExtent;
    m_statics.push_back(s);
}

void World::add_static_mesh(Mesh* mesh, Node3D* node)
{
    StaticCollider s;
    s.shape = ColliderShape::Polygon;
    s.node = node;

    const Mat4& world = node->get_world_matrix();
    const std::vector<MeshVertex>& verts = mesh->vertices();
    const std::vector<u32>& indices = mesh->indices();
    s.tris.reserve(indices.size() / 3);
    for (size_t i = 0; i + 2 < indices.size(); i += 3)
    {
        Triangle t;
        t.v0 = Mat4::Transform(world, verts[indices[i + 0]].position);
        t.v1 = Mat4::Transform(world, verts[indices[i + 1]].position);
        t.v2 = Mat4::Transform(world, verts[indices[i + 2]].position);
        s.tris.push_back(t);
    }
    m_statics.push_back(std::move(s));
}

int World::pushBody(const Body& b)
{
    int handle;
    if (!m_freeList.empty())
    {
        handle = m_freeList.back();
        m_freeList.pop_back();
        m_bodies[(size_t)handle] = b;
    }
    else
    {
        handle = (int)m_bodies.size();
        m_bodies.push_back(b);
    }
    m_dynamicIdx.push_back(handle);
    return handle;
}

int World::add_body(Node3D* node, float radius, CollisionResponse response)
{
    Body b;
    b.node = node;
    b.radius = radius;
    b.height = 0.f;
    b.lastResolvedPos = node->get_global_position();
    b.response = response;
    b.alive = true;
    return pushBody(b);
}

int World::add_capsule_body(Node3D* node, float radius, float height, CollisionResponse response)
{
    Body b;
    b.node = node;
    b.radius = radius;
    b.height = height;
    b.lastResolvedPos = node->get_global_position();
    b.response = response;
    b.alive = true;
    return pushBody(b);
}

void World::remove(int handle)
{
    if (handle < 0 || (size_t)handle >= m_bodies.size() || !m_bodies[(size_t)handle].alive) return;
    m_bodies[(size_t)handle] = Body();
    m_dynamicIdx.erase(std::remove(m_dynamicIdx.begin(), m_dynamicIdx.end(), handle),
                       m_dynamicIdx.end());
    m_freeList.push_back(handle);
}

void World::set_response(int handle, CollisionResponse r)
{
    if (handle < 0 || (size_t)handle >= m_bodies.size() || !m_bodies[(size_t)handle].alive) return;
    m_bodies[(size_t)handle].response = r;
}

const WorldContact& World::last_contact(int handle) const
{
    static const WorldContact empty;
    if (handle < 0 || (size_t)handle >= m_bodies.size() || !m_bodies[(size_t)handle].alive)
        return empty;
    return m_bodies[(size_t)handle].lastContact;
}

// ══════════════════════════════════════════════════════════════════════
// narrow phase — three methods, matching Blitz3D's own hitTest() dispatch
// (COLLISION_METHOD_SPHERE/_BOX/_POLYGON). All fresh code: standard,
// well-known techniques, no code shared with MeshCollision.hpp.
// ══════════════════════════════════════════════════════════════════════

bool World::sphereVsSphere(const Vec3& ca, float ra, const Vec3& cb, float rb, WorldContact& out)
{
    Vec3 delta = cb - ca;
    float dist = delta.length();
    float minDist = ra + rb;
    if (dist >= minDist) return false;

    Vec3 n = dist > 1e-5f ? delta * (1.f / dist) : Vec3(1.f, 0.f, 0.f);
    out.hit = true;
    out.normal = n;
    out.point = ca + n * ra;
    out.depth = minDist - dist;
    return true;
}

bool World::sphereVsBox(const Vec3& center, float radius, const Vec3& boxCenter,
                        const Vec3& halfExtent, WorldContact& out)
{
    Vec3 local = center - boxCenter;
    Vec3 clamped = Vec3::Clamp(local, halfExtent * -1.f, halfExtent);
    Vec3 closest = boxCenter + clamped;
    Vec3 delta = center - closest;
    float dist = delta.length();
    if (dist >= radius) return false;

    out.hit = true;
    if (dist > 1e-5f)
    {
        out.normal = delta * (1.f / dist);
        out.depth = radius - dist;
    }
    else
    {
        // center is inside the box — push out along whichever face is
        // closest (smallest penetration axis)
        Vec3 penetration = halfExtent - Vec3(fabsf(local.x), fabsf(local.y), fabsf(local.z));
        if (penetration.x <= penetration.y && penetration.x <= penetration.z)
        {
            out.normal = Vec3(local.x >= 0.f ? 1.f : -1.f, 0.f, 0.f);
            out.depth = radius + penetration.x;
        }
        else if (penetration.y <= penetration.z)
        {
            out.normal = Vec3(0.f, local.y >= 0.f ? 1.f : -1.f, 0.f);
            out.depth = radius + penetration.y;
        }
        else
        {
            out.normal = Vec3(0.f, 0.f, local.z >= 0.f ? 1.f : -1.f);
            out.depth = radius + penetration.z;
        }
    }
    out.point = closest;
    return true;
}

// closest point on a triangle to `p` — standard region-based projection
// (project onto the plane, clamp back into the triangle if the projection
// lands outside an edge).
static Vec3 closestPointOnTriangle(const Vec3& p, const Triangle& t)
{
    Vec3 ab = t.v1 - t.v0, ac = t.v2 - t.v0, ap = p - t.v0;
    float d1 = Vec3::Dot(ab, ap), d2 = Vec3::Dot(ac, ap);
    if (d1 <= 0.f && d2 <= 0.f) return t.v0; // vertex region v0

    Vec3 bp = p - t.v1;
    float d3 = Vec3::Dot(ab, bp), d4 = Vec3::Dot(ac, bp);
    if (d3 >= 0.f && d4 <= d3) return t.v1; // vertex region v1

    float vc = d1 * d4 - d3 * d2;
    if (vc <= 0.f && d1 >= 0.f && d3 <= 0.f)
    {
        float v = d1 / (d1 - d3);
        return t.v0 + ab * v; // edge v0-v1
    }

    Vec3 cp = p - t.v2;
    float d5 = Vec3::Dot(ab, cp), d6 = Vec3::Dot(ac, cp);
    if (d6 >= 0.f && d5 <= d6) return t.v2; // vertex region v2

    float vb = d5 * d2 - d1 * d6;
    if (vb <= 0.f && d2 >= 0.f && d6 <= 0.f)
    {
        float w = d2 / (d2 - d6);
        return t.v0 + ac * w; // edge v0-v2
    }

    float va = d3 * d6 - d5 * d4;
    if (va <= 0.f && (d4 - d3) >= 0.f && (d5 - d6) >= 0.f)
    {
        float w = (d4 - d3) / ((d4 - d3) + (d5 - d6));
        return t.v1 + (t.v2 - t.v1) * w; // edge v1-v2
    }

    // face region
    float denom = 1.f / (va + vb + vc);
    float v = vb * denom, w = vc * denom;
    return t.v0 + ab * v + ac * w;
}

bool World::sphereVsMesh(const Vec3& center, float radius, const std::vector<Triangle>& tris,
                         WorldContact& out)
{
    // brute-force over every triangle — no box-tree broad-phase (that's
    // MeshCollider's trick, off-limits here); fine at level.ms3d's scale
    // (hundreds of triangles), see World.hpp's header comment.
    for (const Triangle& t : tris)
    {
        // skip degenerate (zero-area) triangles — closestPointOnTriangle's
        // face-region branch divides by (va+vb+vc), which is ~0 for a
        // degenerate triangle and produces NaN/Inf that then silently
        // reads as "hit" (any comparison against NaN is false, so the
        // dist>=radius reject below never fires). Hand-authored meshes
        // occasionally have a sliver/duplicate-vertex triangle; this is
        // cheap insurance against that, not a sign the algorithm is wrong
        // for well-formed ones.
        Vec3 ab = t.v1 - t.v0, ac = t.v2 - t.v0;
        if (Vec3::Cross(ab, ac).length_squared() < 1e-12f) continue;

        Vec3 closest = closestPointOnTriangle(center, t);
        Vec3 delta = center - closest;
        float dist = delta.length();
        if (dist >= radius) continue;

        out.hit = true;
        out.point = closest;
        out.normal = dist > 1e-5f ? delta * (1.f / dist) : t.normal();
        out.depth = radius - dist;
        return true; // first hit only, matches the documented v1 limit
    }
    return false;
}

// ══════════════════════════════════════════════════════════════════════
// capsule-mover narrow phase — a capsule is a sphere swept along a
// segment, so capsuleVsSphere/Box both reduce to "find the segment's
// closest point to the target, then run the existing sphere test from
// there". Only capsuleVsMesh needs real segment-vs-triangle math (a
// sphere's single center point isn't enough once the target is also
// extended along a line).
// ══════════════════════════════════════════════════════════════════════

// closest point on segment [a,b] to point p
static Vec3 closestPointOnSegment(const Vec3& p, const Vec3& a, const Vec3& b)
{
    Vec3 ab = b - a;
    float len2 = ab.length_squared();
    if (len2 < 1e-12f) return a;
    float t = Vec3::Dot(p - a, ab) / len2;
    t = t < 0.f ? 0.f : (t > 1.f ? 1.f : t);
    return a + ab * t;
}

// closest points between two segments [p1,q1] and [p2,q2] (Ericson,
// "Real-Time Collision Detection" — the standard algorithm for this).
static void closestPointsSegmentSegment(const Vec3& p1, const Vec3& q1, const Vec3& p2,
                                        const Vec3& q2, Vec3& outC1, Vec3& outC2)
{
    Vec3 d1 = q1 - p1, d2 = q2 - p2, r = p1 - p2;
    float a = d1.length_squared(), e = d2.length_squared();
    float f = Vec3::Dot(d2, r);

    float s, t;
    if (a < 1e-12f && e < 1e-12f)
    {
        outC1 = p1;
        outC2 = p2;
        return;
    }
    if (a < 1e-12f)
    {
        s = 0.f;
        t = f / e;
        t = t < 0.f ? 0.f : (t > 1.f ? 1.f : t);
    }
    else
    {
        float c = Vec3::Dot(d1, r);
        if (e < 1e-12f)
        {
            t = 0.f;
            s = -c / a;
            s = s < 0.f ? 0.f : (s > 1.f ? 1.f : s);
        }
        else
        {
            float b = Vec3::Dot(d1, d2);
            float denom = a * e - b * b;
            s = denom > 1e-12f ? (b * f - c * e) / denom : 0.f;
            s = s < 0.f ? 0.f : (s > 1.f ? 1.f : s);
            t = (b * s + f) / e;
            if (t < 0.f)
            {
                t = 0.f;
                s = -c / a;
                s = s < 0.f ? 0.f : (s > 1.f ? 1.f : s);
            }
            else if (t > 1.f)
            {
                t = 1.f;
                s = (b - c) / a;
                s = s < 0.f ? 0.f : (s > 1.f ? 1.f : s);
            }
        }
    }
    outC1 = p1 + d1 * s;
    outC2 = p2 + d2 * t;
}

bool World::capsuleVsSphere(const Vec3& segA, const Vec3& segB, float radius, const Vec3& cb,
                            float rb, WorldContact& out)
{
    Vec3 closest = closestPointOnSegment(cb, segA, segB);
    return sphereVsSphere(closest, radius, cb, rb, out);
}

bool World::capsuleVsBox(const Vec3& segA, const Vec3& segB, float radius, const Vec3& boxCenter,
                         const Vec3& halfExtent, WorldContact& out)
{
    // iterative closest point between the segment and the box: ping-pong
    // projecting a running guess onto the box, then back onto the segment,
    // a few times — converges quickly for a convex box and a segment of
    // this length; once we have the segment's closest point, the existing
    // sphereVsBox already handles both the outside and the inside-the-box
    // cases correctly from there.
    Vec3 p = (segA + segB) * 0.5f;
    for (int i = 0; i < 4; ++i)
    {
        Vec3 local = p - boxCenter;
        Vec3 clamped = Vec3::Clamp(local, halfExtent * -1.f, halfExtent);
        Vec3 boxPoint = boxCenter + clamped;
        p = closestPointOnSegment(boxPoint, segA, segB);
    }
    return sphereVsBox(p, radius, boxCenter, halfExtent, out);
}

bool World::capsuleVsMesh(const Vec3& segA, const Vec3& segB, float radius,
                          const std::vector<Triangle>& tris, WorldContact& out)
{
    for (const Triangle& t : tris)
    {
        Vec3 ab = t.v1 - t.v0, ac = t.v2 - t.v0;
        if (Vec3::Cross(ab, ac).length_squared() < 1e-12f) continue; // degenerate triangle, skip

        // closest points between the capsule's segment and the triangle:
        // standard technique is the best of (a) each segment endpoint
        // against the triangle and (b) each triangle edge against the
        // segment — there's no single closed-form shortcut the way there
        // is for two segments or a segment and a plane.
        Vec3 bestOnSeg = segA;
        Vec3 bestOnTri = closestPointOnTriangle(segA, t);
        float bestDist2 = (bestOnSeg - bestOnTri).length_squared();

        Vec3 triClosestToB = closestPointOnTriangle(segB, t);
        float d2 = (segB - triClosestToB).length_squared();
        if (d2 < bestDist2)
        {
            bestDist2 = d2;
            bestOnSeg = segB;
            bestOnTri = triClosestToB;
        }

        const Vec3 edgeStart[3] = {t.v0, t.v1, t.v2};
        const Vec3 edgeEnd[3] = {t.v1, t.v2, t.v0};
        for (int e = 0; e < 3; ++e)
        {
            Vec3 c1, c2;
            closestPointsSegmentSegment(segA, segB, edgeStart[e], edgeEnd[e], c1, c2);
            float dd = (c1 - c2).length_squared();
            if (dd < bestDist2)
            {
                bestDist2 = dd;
                bestOnSeg = c1;
                bestOnTri = c2;
            }
        }

        float dist = sqrtf(bestDist2);
        if (dist >= radius) continue;

        out.hit = true;
        out.point = bestOnTri;
        out.normal = dist > 1e-5f ? (bestOnSeg - bestOnTri) * (1.f / dist) : t.normal();
        out.depth = radius - dist;
        return true; // first hit only, matches the documented v1 limit
    }
    return false;
}

// ══════════════════════════════════════════════════════════════════════
// per-frame update
// ══════════════════════════════════════════════════════════════════════

void World::update(float dt)
{
    (void)dt; // unused for now — kept for API symmetry with Scene::update/AnimationPlayer::update
    for (int idx : m_dynamicIdx)
    {
        Body& b = m_bodies[(size_t)idx];

        Vec3 curPos = b.node->get_global_position();
        Vec3 moveVec = curPos - b.lastResolvedPos; // this frame's intended displacement

        // capsule mover: segment centered on curPos, standing upright
        // (local Y) — half-length is 0 (degenerates to the plain sphere
        // path) whenever height doesn't exceed the two caps' own radius
        bool isCapsule = b.height > b.radius * 2.f + 1e-5f;
        float halfSeg = isCapsule ? (b.height * 0.5f - b.radius) : 0.f;
        Vec3 segA = curPos - Vec3(0.f, halfSeg, 0.f);
        Vec3 segB = curPos + Vec3(0.f, halfSeg, 0.f);

        WorldContact hitInfo;
        bool hit = false;
        for (const StaticCollider& s : m_statics)
        {
            WorldContact info;
            bool thisHit = false;
            if (isCapsule)
            {
                switch (s.shape)
                {
                    case ColliderShape::Sphere:
                        thisHit = capsuleVsSphere(segA, segB, b.radius, s.node->get_global_position(),
                                                  s.radius, info);
                        break;
                    case ColliderShape::Box:
                        thisHit = capsuleVsBox(segA, segB, b.radius, s.node->get_global_position(),
                                              s.halfExtent, info);
                        break;
                    case ColliderShape::Polygon:
                        thisHit = capsuleVsMesh(segA, segB, b.radius, s.tris, info);
                        break;
                }
            }
            else
            {
                switch (s.shape)
                {
                    case ColliderShape::Sphere:
                        thisHit = sphereVsSphere(curPos, b.radius, s.node->get_global_position(),
                                                 s.radius, info);
                        break;
                    case ColliderShape::Box:
                        thisHit = sphereVsBox(curPos, b.radius, s.node->get_global_position(),
                                             s.halfExtent, info);
                        break;
                    case ColliderShape::Polygon:
                        thisHit = sphereVsMesh(curPos, b.radius, s.tris, info);
                        break;
                }
            }
            if (thisHit)
            {
                hit = true;
                hitInfo = info;
                break; // first hit only, v1 (see World.hpp's header comment)
            }
        }

        // body-vs-body still treats every body as a plain sphere (b.radius,
        // ignoring height) even if it's a capsule — capsule-vs-capsule
        // isn't wired up (out of scope: this round only asked for capsule
        // movers against the static world, not against each other).
        if (!hit && m_bodyVsBody)
        {
            for (int otherIdx : m_dynamicIdx)
            {
                if (otherIdx == idx) continue;
                Body& other = m_bodies[(size_t)otherIdx];
                WorldContact info;
                if (sphereVsSphere(curPos, b.radius, other.node->get_global_position(),
                                   other.radius, info))
                {
                    hit = true;
                    hitInfo = info;
                    break;
                }
            }
        }

        b.lastContact = hit ? hitInfo : WorldContact();
        if (hit)
        {
            if (b.response == CollisionResponse::Stop)
            {
                moveVec = Vec3(0.f, 0.f, 0.f);
            }
            else
            {
                float into = Vec3::Dot(moveVec, hitInfo.normal);
                if (into < 0.f) moveVec -= hitInfo.normal * into;
                if (b.response == CollisionResponse::SlideXZ) moveVec.y = 0.f;
            }
            curPos = b.lastResolvedPos + moveVec + hitInfo.normal * hitInfo.depth;
        }

        b.node->set_position(curPos);
        b.lastResolvedPos = curPos;
    }
}
