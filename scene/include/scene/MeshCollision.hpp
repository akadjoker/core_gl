#pragma once

// Mesh-vs-mesh collision: a small broad-phase box-tree per Mesh (built once,
// cached, shared by every instance of that Mesh) plus an exact
// triangle-triangle narrow phase, giving a real contact point/normal/depth
// instead of the sphere-sphere approximation games/hoverjet uses today (see
// HoverPhysics.hpp's resolveShipCollision).
//
// Own implementation — not a port of any third-party library. The box-tree
// leaves store triangle *indices* into the Mesh's own vertex/index buffers
// (Mesh::vertices()/indices(), already CPU-resident — see Mesh::free_cpu()),
// never a copy of the geometry. The tree is cached per Mesh* so N instances
// sharing one Mesh (every NPC ship, say) build it once.
//
// This is a poll API, not an event system: MeshCollider::test()/
// CollisionRegistry::test() are called explicitly by game code, mirroring
// how HoverPhysics.hpp's resolveShipCollision is called today — no hidden
// per-frame auto-pairing. CollisionRegistry::test_and_notify() is a thin
// optional convenience for callback-style consumers (a future scripting VM
// registers a callback there; the core never depends on it).

#include "scene/Math.hpp" // Vec3, Mat4, BoundingBox, Triangle
#include <vector>

class Mesh;
class Node3D;

// result of a collision test: first colliding triangle pair found (broad
// phase + narrow phase both stop at the first hit — see MeshCollision.cpp
// for why a full contact manifold is out of scope).
struct ContactInfo
{
    bool  hit = false;
    Vec3  point{0.f, 0.f, 0.f};  // midpoint of the two triangles' overlap segment
    Vec3  normal{0.f, 0.f, 0.f}; // triangle A's world-space face normal
    // approximation, not true SAT/EPA penetration depth: max distance any
    // vertex of triangle B sits behind triangle A's plane. Cheap and good
    // enough for gameplay reactions (sparks, push-apart, damage), not a
    // physics-grade solver input.
    float depth = 0.f;
    u32   triIndexA = 0, triIndexB = 0;
};

// Mesh geometry reference + broad-phase box-tree + narrow-phase test, all
// in one place. Never owns the Mesh. Everything needed to test two of these
// against each other lives on this class — build, tree, and the
// triangle-triangle math are all private implementation details.
class MeshCollider
{
public:
    // leaves hold up to kMaxLeafTriangles triangles each, not one — fewer,
    // "fatter" leaves means fewer nodes to traverse for anything bigger
    // than a toy mesh, and the narrow-phase brute force inside a matching
    // leaf pair (worst case kMaxLeafTriangles²) stays cheap.
    static const u32 kMaxLeafTriangles = 8;

    struct Node
    {
        BoundingBox bounds;
        int left = -1, right = -1;      // child node indices; -1,-1 => leaf
        u32 leafStart = 0, leafCount = 0; // valid only when leaf: range into m_leafTriangles
        bool is_leaf() const { return left < 0; }
    };

    // Returns the cached collider for `mesh`, building its box-tree on
    // first use. Lives for the program's lifetime (Mesh* assets already do,
    // in this engine — see AssetManager) — callers never free the pointer.
    static MeshCollider* get_or_create(Mesh* mesh);

    bool empty() const { return m_nodes.empty(); }
    const Mesh* mesh() const { return m_mesh; }

    // Core query: tests `this` (in world space via worldSelf) against
    // `other` (via worldOther). Returns true and fills `out` on the first
    // colliding triangle pair found; `out.hit = false` and `out` otherwise
    // untouched on a miss.
    bool test(const Mat4& worldSelf, const MeshCollider& other, const Mat4& worldOther,
             ContactInfo& out) const;

private:
    void build(const Mesh* mesh);
    int  build_range(std::vector<u32>& triIndices, size_t begin, size_t end);
    // fetches triangle `triIndex`'s 3 vertex positions from the owning
    // Mesh's own buffers — never copied/cached, always a fresh lookup.
    Triangle triangle_at(u32 triIndex) const;

    // narrow phase: exact triangle-triangle intersection, adapted to this
    // engine's Vec3/Mat4 — same well-known technique (plane/plane
    // intersection line, interval overlap of each triangle's projection
    // onto that line, Möller 1997) as the reference library we studied
    // while designing this, rewritten from scratch against our own types.
    static bool triangle_triangle(const Triangle& a, const Triangle& b, ContactInfo& out);
    // finds triangle `t`'s two edge/plane crossing points against the
    // other triangle's plane (normal n, offset d); false if `t` doesn't
    // straddle that plane at all (used as an early-out).
    static bool edge_plane_crossings(const Triangle& t, const Vec3& n, float d, Vec3& outP0,
                                     Vec3& outP1);

    const Mesh* m_mesh = nullptr;
    std::vector<Node> m_nodes;
    std::vector<u32> m_leafTriangles; // flat storage; leaves reference a [start,start+count) range
};

// ── registry: add/remove (Mesh*, Node3D*) pairs, external to the scene tree ──
class CollisionRegistry
{
public:
    // builds/reuses `mesh`'s cached box-tree; returns a handle valid until
    // remove() is called with it. `node` is not owned.
    int  add(Mesh* mesh, Node3D* node);
    void remove(int handle);
    Node3D* node_of(int handle) const;

    // world transform comes from node->get_world_matrix() at call time —
    // always tests current poses, nothing cached per-pair
    bool test(int handleA, int handleB, ContactInfo& out) const;

    using CollisionCallback = void (*)(Node3D* a, Node3D* b, const ContactInfo& info,
                                       void* userData);
    // same as test(), and additionally invokes `cb` on a hit — the seam a
    // future scripting VM hooks into, without the core depending on it
    bool test_and_notify(int handleA, int handleB, CollisionCallback cb, void* userData) const;

private:
    struct Entry
    {
        MeshCollider* collider = nullptr;
        Node3D* node = nullptr;
        bool alive = false;
    };
    std::vector<Entry> m_entries;
    std::vector<int> m_freeList;
};
