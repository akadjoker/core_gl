#pragma once

// A small, fully self-contained automation layer for collision, modeled on
// Blitz3D's own World/UpdateWorld/Collisions system (studied directly from
// the user's Blitz3D install — tmp/blitz3d/world.cpp for the response
// codes and the three collision methods, and a real sample,
// collision_sliding.bb, for how game code actually drives it).
//
// Deliberately zero dependency on scene/MeshCollision.hpp: that system
// already backs games/hoverjet and demo_mesh_collision_test, and this is a
// separate learning/experimentation piece that must never risk it — every
// bit of collision math here (sphere-vs-sphere, sphere-vs-box,
// sphere-vs-mesh) is its own code, not shared with MeshCollider.
//
// Three collision methods, matching Blitz3D's own hitTest() dispatch
// (COLLISION_METHOD_SPHERE/_POLYGON/_BOX): the mover (a "body") is always
// a sphere, same as Blitz3D's EntityRadius; the target (static world
// geometry) can be a Sphere, a Box, or a Polygon (mesh) — one narrow-phase
// test per combination, not a full shape-vs-shape matrix.
//
// Deliberately NOT a physics system: no stored velocity or gravity, same
// as Blitz3D's own UpdateWorld (see collision_sliding.bb — those are
// plain game-side variables applied by hand every frame via
// TranslateEntity, entirely outside collision). A body's "movement" each
// update() is reconstructed implicitly, by comparing its node's current
// position against wherever World last resolved it to — game code is free
// to move that node however it wants between update() calls.
//
// Kept as its own class, separate from Scene/Node — a self-contained
// experiment/learning piece, not meant to reach into or change the scene
// graph itself.

#include "scene/Math.hpp" // Vec3, Triangle
#include <vector>

class Mesh;
class Node3D;

enum class ColliderShape
{
    Sphere,
    Box,
    Polygon
};

enum class CollisionResponse
{
    Stop,    // cancel this frame's movement entirely on contact — a hard wall
    Slide,   // project the movement onto the contact plane (full 3D slide)
    SlideXZ  // same, but the vertical component of the slide is discarded
             // (keeps a walking character from sliding up/down a slanted
             // wall — FPS-style movement almost always wants this over Slide)
};

// World's own contact resul 
struct WorldContact
{
    bool hit = false;
    Vec3 point{0.f, 0.f, 0.f};
    Vec3 normal{0.f, 0.f, 0.f};
    float depth = 0.f;
};

class World
{
public:
    // three ways to register static, non-moving world geometry — one per
    // ColliderShape,  `node` is not
    // owned.
    void add_static_sphere(Node3D* node, float radius);
    // world-axis-aligned box (not a real OBB — see World.cpp's header
    // comment for the faithful-OBB follow-up)
    void add_static_box(Node3D* node, const Vec3& halfExtent);
    // bakes `mesh`'s own vertex/index buffers, transformed by `node`'s
    // CURRENT world matrix, into World's own world-space Triangle list —
    // own copy, own Triangle usage (Math.hpp's Triangle, not anything
    // from MeshCollision.hpp). Call after `node` is in its final resting
    // pose, since static geometry isn't expected to move afterward.
    void add_static_mesh(Mesh* mesh, Node3D* node);

    // a moving body — always a sphere (the mover ). `node`
    // is not owned; World only ever moves it to correct a collision.
    int add_body(Node3D* node, float radius, CollisionResponse response = CollisionResponse::Slide);
    // same idea, shaped like a capsule instead: a sphere swept along a
    // segment centered on `node`, standing upright (local Y). `height` is
    // the TOTAL capsule height including both hemispherical caps — the
    // straight cylindrical part in between is `height - 2*radius`
    // (clamped to 0, so a small height just degenerates to a sphere).
    // Standard shape for a walking character: covers head-to-feet without
    // needing one huge sphere radius. Everything else about a body
    // (response, last_contact, WASD-drives-the-node, ...) works exactly
    // the same either way.
    int add_capsule_body(Node3D* node, float radius, float height,
                         CollisionResponse response = CollisionResponse::Slide);
    void remove(int handle);
    void set_response(int handle, CollisionResponse r);

    // last update()'s hit info for this body, if any — read after
    // update() for a "grounded" check (info.normal.y > ~0.5, same idea as
    // Blitz3D's CollisionNY) or to spawn a spark/sound.
    const WorldContact& last_contact(int handle) const;

    // dynamic-vs-dynamic (always sphere-vs-sphere, since bodies are always
    // spheres) — off by default: O(n^2) pairs, not every scene needs
    // bodies pushing each other.
    void set_body_vs_body(bool on) { m_bodyVsBody = on; }

    // one frame, per body: reconstructs this frame's intended movement as
    // (node's current position - the position World left it at after the
    // previous update()) — not a stored velocity, the game may have moved
    // the node any amount, any way, since the last call — tests the
    // current position against every static (dispatched by that static's
    // ColliderShape) and, if enabled, every other body, and on a hit
    // resolves per that body's response (Stop/Slide/SlideXZ).
    void update(float dt);

private:
    // the three sphere-mover narrow-phase tests — all self-contained, no
    // shared code with MeshCollision.hpp:
    static bool sphereVsSphere(const Vec3& ca, float ra, const Vec3& cb, float rb, WorldContact& out);
    static bool sphereVsBox(const Vec3& center, float radius, const Vec3& boxCenter,
                            const Vec3& halfExtent, WorldContact& out);
    static bool sphereVsMesh(const Vec3& center, float radius, const std::vector<Triangle>& tris,
                             WorldContact& out);
    // same three, capsule-mover version (segment [segA,segB] + radius) —
    // each reduces to the sphere version at the segment's closest point,
    // except sphereVsMesh's true segment-vs-triangle needs its own math
    // (see World.cpp).
    static bool capsuleVsSphere(const Vec3& segA, const Vec3& segB, float radius, const Vec3& cb,
                                float rb, WorldContact& out);
    static bool capsuleVsBox(const Vec3& segA, const Vec3& segB, float radius,
                             const Vec3& boxCenter, const Vec3& halfExtent, WorldContact& out);
    static bool capsuleVsMesh(const Vec3& segA, const Vec3& segB, float radius,
                              const std::vector<Triangle>& tris, WorldContact& out);

    struct StaticCollider
    {
        ColliderShape shape = ColliderShape::Sphere;
        Node3D* node = nullptr;
        float radius = 0.f;         // Sphere
        Vec3 halfExtent{0.f, 0.f, 0.f}; // Box
        std::vector<Triangle> tris; // Polygon — baked world-space at add_static_mesh() time
    };
    struct Body
    {
        Node3D* node = nullptr;
        float radius = 0.f;
        float height = 0.f; // 0 = sphere mover; >0 = capsule, total height incl. both caps
        Vec3 lastResolvedPos{0.f, 0.f, 0.f};
        CollisionResponse response = CollisionResponse::Slide;
        WorldContact lastContact;
        bool alive = false;
    };
    // shared add_body()/add_capsule_body() bookkeeping: handle reuse via
    // m_freeList, registers in m_dynamicIdx
    int pushBody(const Body& b);

    std::vector<StaticCollider> m_statics;
    std::vector<Body> m_bodies;
    std::vector<int> m_freeList;
    std::vector<int> m_dynamicIdx; // indices into m_bodies
    bool m_bodyVsBody = false;
};
