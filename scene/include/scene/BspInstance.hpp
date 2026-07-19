#pragma once

#include "scene/Node3D.hpp"
#include "scene/Math.hpp" // Vec3
#include <vector>
#include <string>
#include <unordered_map>

class Mesh;
class Material;
namespace assets { class AssetManager; }

 
enum class BspCollisionResponse
{
    Stop,   // cancel the remaining movement entirely on contact
    Slide,  // project the remaining movement onto the contact plane
    SlideXZ // same, but the vertical component of the slide is discarded
};

// Result of a single BspInstance::traceBox call — a swept-box trace through
// the map's BSP tree/brushes, Quake-style.
struct BspTraceResult
{
    bool hit = false;
    bool startSolid = false; // the box already started inside solid geometry
    float fraction = 1.f;    // 0 = didn't move at all, 1 = reached `end` untouched
    Vec3 endPos{0.f, 0.f, 0.f};
    Vec3 normal{0.f, 0.f, 0.f}; // the blocking plane's normal, if hit
};

// Result of BspInstance::raycast — same underlying trace as traceBox with a
// zero half-extent, just named/shaped for point queries (picking, line of
// sight, "what's the player looking at") rather than sweeping a body.
struct BspRayHit
{
    bool hit = false;
    Vec3 point{0.f, 0.f, 0.f};  // world-space (raw map-unit) hit position
    Vec3 normal{0.f, 0.f, 0.f}; // surface normal at the hit
    float distance = 0.f;      // distance from origin to point, raw map units
};

// BSP lightmapped mesh instance.  Identical mesh/material API to MeshInstance
// but the renderer collects it separately and draws it with the BSP shader
// (uv2 lightmap, no tangents, no CSM shadows).
//
// Also carries the map's own collision data (planes/nodes/leafs/brushes,
// loaded by AssetManager::load_bsp_collision — see BSPLoader.cpp) and its
// parsed entity list (spawn points for now; classname dispatch is a later
// step). Deliberately self-contained here rather than a separate class: the
// BSP tree IS this map's own geometry, so its collision belongs with it.
class BspInstance : public Node3D
{
public:
    static constexpr NodeType ClassType = NT_BSPINSTANCE;

    explicit BspInstance(const std::string& name = "BspInstance")
        : Node3D(name), m_mesh(nullptr)
    {
        m_type = NT_BSPINSTANCE;
    }

 

    bool is_a(NodeType t) const override { return t == NT_BSPINSTANCE || Node3D::is_a(t); }

    void set_mesh(Mesh* mesh) { m_mesh = mesh; }
    Mesh* get_mesh() const { return m_mesh; }

    void set_material(Material* m) { m_materials.assign(1, m); }
    void set_materials(const std::vector<Material*>& m) { m_materials = m; }
    const std::vector<Material*>& get_materials() const { return m_materials; }

    bool visible = true;

    // ---- collision (populated by AssetManager::load_bsp_collision) --------

    bool has_collision() const { return !m_bspNodes.empty(); }

    // Sweeps a box (halfExtent — zero for a point trace) from `start` to
    // `end` through the BSP tree, stopping at the first solid brush.
    BspTraceResult traceBox(const Vec3& start, const Vec3& end, const Vec3& halfExtent) const;

    // Convenience "drive a body" helper: traces the intended move and, on a
    // hit, resolves per `response` (Stop/Slide/SlideXZ), iterating a few
    // passes so a corner doesn't just halt movement outright. Returns the
    // corrected end position; `outGrounded`, if given, is set when the last
    // contact's normal points mostly straight up (walking-surface check).
    Vec3 moveAndSlide(const Vec3& start, const Vec3& end, const Vec3& halfExtent,
                      BspCollisionResponse response, bool* outGrounded = nullptr) const;


    Vec3 tryStepUp(const Vec3& start, const Vec3& end, const Vec3& halfExtent,
    bool* outStepped, bool* outGrounded) const;

    // Point query (zero-size traceBox) along a ray — picking, line-of-sight
    // checks, "what wall is the crosshair on". `direction` need not be
    // normalized; `maxDistance` bounds the search (defaults to a generous
    // 8192 raw map units, ~256m).
    BspRayHit raycast(const Vec3& origin, const Vec3& direction, float maxDistance = 8192.f) const;

   
    // ---- entities (populated by AssetManager::load_bsp_collision) ---------
    // Phase A: just the raw parsed classname+keyvalue list, no factory/
    // dispatch yet (movers/doors/triggers are a later step).
    struct Entity
    {
        std::string classname;
        std::unordered_map<std::string, std::string> keyValues;
    };
    const std::vector<Entity>& entities() const { return m_entities; }

    // First "info_player_start" (falls back to "info_player_deathmatch")
    // entity's "origin", converted to engine Y-up. Returns false if none
    // found (outPos left untouched).
    bool find_spawn_point(Vec3& outPos) const;

    // Brush entities (doors, triggers, water, ...) have no "origin" — their
    // location/extent is their own referenced brush model instead, named by
    // a "model" key like "*3" (index 3 into the BSP's models lump; model 0
    // is always worldspawn, so entities never reference it). Looks up that
    // model's bounding box (engine Y-up) — used for debug gizmos now, and
    // will be what a later mover/door implementation transforms. False if
    // `e` has no "model" key or it doesn't resolve to a loaded model.
    bool entity_model_bounds(const Entity& e, Vec3& outMin, Vec3& outMax) const;

    // Any entity's own "origin" key (engine Y-up), for point entities
    // (lights, spawns, ...) that aren't brush models. Static: it's pure
    // text parsing, no instance state needed.
    static bool entity_origin(const Entity& e, Vec3& outPos);

    // Drops every entity whose classname matches (maps can carry a lot of
    // noise you don't care about yet — e.g. oa_rpg3dm2's 31 target_speaker
    // entries). Returns how many were removed.
    int remove_entities(const std::string& classname);

private:
    Mesh* m_mesh;
    std::vector<Material*> m_materials;
 

    // ---- collision data (Quake 3 IBSP planes/nodes/leafs/brushes) --------
    struct BspPlane { Vec3 normal{0.f,0.f,0.f}; float dist = 0.f; };
    struct BspNode { int planeIndex = 0; int children[2] = {0, 0}; };
    struct BspLeaf { int firstLeafBrush = 0; int numLeafBrushes = 0; };
    struct BspBrushSide { int planeIndex = 0; };
    struct BspBrush { int firstSide = 0; int numSides = 0; bool solid = false; };
    struct BspModel { Vec3 mins{0.f,0.f,0.f}; Vec3 maxs{0.f,0.f,0.f}; int firstBrush = 0; int numBrushes = 0; };

    std::vector<BspPlane> m_bspPlanes;
    std::vector<BspNode> m_bspNodes;
    std::vector<BspLeaf> m_bspLeafs;
    std::vector<int> m_bspLeafBrushes;
    std::vector<BspBrush> m_bspBrushes;
    std::vector<BspBrushSide> m_bspBrushSides;
    std::vector<BspModel> m_bspModels; // [0] = worldspawn (unused directly; brush entities reference [1..])
    std::vector<Entity> m_entities;
    int m_bspRootNode = 0;

    // Recursive box trace (Quake's classic CM_RecursiveHullCheck shape) and
    // leaf/brush clip — implemented in BSPLoader.cpp alongside the parsing
    // that fills the members above. `start`/`end` here are the SPLIT
    // sub-segment used purely to decide which side(s) of each node to
    // descend into (broadphase); `origStart`/`origEnd` are the trace's real
    // start/end, carried through unchanged and passed to traceLeaf/
    // clipBoxToBrush — the brush test always runs against the FULL original
    // sweep, never a local slice, so its resulting fraction is already in
    // the right 0..1 scale with no remap needed (same convention as
    // apocalyx's GLBsp::checkMoveNode/clipBoxToBrush, which keeps a single
    // persistent collision.start/end for exactly this reason — verified
    // against tmp/apocalyx/glbsp.cpp directly after an earlier version of
    // this code passed the split sub-segment down and silently missed most
    // real collisions).
    void traceNode(int node, float startFrac, float endFrac, const Vec3& start, const Vec3& end,
                  const Vec3& origStart, const Vec3& origEnd, const Vec3& halfExtent,
                  BspTraceResult& result) const;
    void traceLeaf(int leaf, const Vec3& origStart, const Vec3& origEnd, const Vec3& halfExtent,
                  BspTraceResult& result) const;
    bool clipBoxToBrush(const BspBrush& brush, const Vec3& origStart, const Vec3& origEnd,
                        const Vec3& halfExtent, BspTraceResult& result) const;

    // Parses the collision-relevant lumps (planes/nodes/leafs/brushes/
    // models/entities) straight out of an already-in-memory .bsp file and
    // fills this instance's own m_bsp*/m_entities members. `data`/`dataSize`
    // are the whole file's bytes — magic/version/size are assumed already
    // validated by the caller. Takes raw bytes (not a path) specifically so
    // AssetManager::load_bsp_mesh can call this against the SAME buffer it
    // already read for the render mesh, instead of re-reading the file a
    // second time (see BSPLoader.cpp).
    void loadCollisionFromBytes(const unsigned char* data, unsigned int dataSize, const char* path);

    friend class assets::AssetManager;
};
