#pragma once

#include "scene/Node3D.hpp"
#include "scene/Math.hpp" // Vec3
#include <vector>
#include <string>
#include <unordered_map>

class Mesh;
class Material;
class MeshInstance;
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

// Quake 3 brush CONTENTS_* flags, as stored raw per-brush (see
// BspInstance::worldCollision). Only the ones we can act on today are
// listed; add more here as needed, they're just the format's own bits.
namespace BspContents
{
constexpr int Solid = 1;          // CONTENTS_SOLID
constexpr int Water = 32;         // CONTENTS_WATER
constexpr int PlayerClip = 0x10000; // CONTENTS_PLAYERCLIP — invisible player-only blockers
// Default "blocks the player" mask — exactly what traceBox/moveAndSlide
// have always used. worldCollision() lets a caller pass something else
// (e.g. Solid|Water to also stop at water surfaces) without changing what
// traceBox/moveAndSlide do.
constexpr int DefaultSolid = Solid | PlayerClip;
} // namespace BspContents

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
    // `end` through the BSP tree, stopping at the first solid brush. Always
    // uses BspContents::DefaultSolid — unchanged behavior, exactly what it
    // always did. For a caller-chosen content mask (water, triggers, ...)
    // see worldCollision() below instead of changing this one.
    BspTraceResult traceBox(const Vec3& start, const Vec3& end, const Vec3& halfExtent) const;

    // Same trace, but the caller picks which CONTENTS_* bits count as
    // blocking (BspContents::*, OR them together) instead of the fixed
    // "solid to the player" default traceBox always uses. Modeled directly
    // on Genesis3D's geWorld_Collision (tmp/Genesis3D11/src/Game/GMain.c —
    // Mins/Maxs + Pos1/Pos2 + a content mask, same shape) — this is the
    // general query traceBox/moveAndSlide are themselves built on top of,
    // exposed for callers that need a non-default mask (e.g. "does this ray
    // cross water" without also stopping at solid geometry along the way).
    // Doesn't yet identify WHICH entity/model was hit (Genesis3D's
    // Collision.Model) — worldspawn's static tree only, for now; brush
    // entities/movers are a separate future step.
    BspTraceResult worldCollision(const Vec3& start, const Vec3& end, const Vec3& halfExtent,
                                  int contentMask) const;

    // Convenience "drive a body" helper: traces the intended move and, on a
    // hit, resolves per `response` (Stop/Slide/SlideXZ), iterating a few
    // passes so a corner doesn't just halt movement outright. Returns the
    // corrected end position; `outGrounded`, if given, is set when the last
    // contact's normal points mostly straight up (walking-surface check).
    Vec3 moveAndSlide(const Vec3& start, const Vec3& end, const Vec3& halfExtent,
                      BspCollisionResponse response, bool* outGrounded = nullptr) const;


    Vec3 tryStepUp(const Vec3& start, const Vec3& end, const Vec3& halfExtent,
    bool* outStepped, bool* outGrounded) const;

    // New, separate mover built on worldCollision() — deliberately NOT
    // touching moveAndSlide, which stays exactly as it was. Modeled on
    // Genesis3D's CheckVelocity (tmp/Genesis3D11/src/Game/GMain.c): takes a
    // PERSISTENT velocity (in/out, units/second — not a one-shot position
    // delta like moveAndSlide's `end`) and mutates it in place across up to
    // 4 collision passes this frame, same as their Player->Velocity. The
    // piece moveAndSlide's simple single-plane projection doesn't have:
    // when the same pass gets blocked by a SECOND plane without making any
    // real progress (a corner, or a ramp meeting a wall), it slides along
    // the CROSS PRODUCT of both plane normals — the crease line where they
    // meet — instead of the two projections fighting each other and
    // reading as "stuck". Returns the resolved position for this frame;
    // `velocity` is left as whatever remains usable next frame (already
    // clipped, matching Genesis3D's own "keep it, don't reset to zero
    // every frame" convention).
    Vec3 slideVelocity(const Vec3& start, Vec3& velocity, const Vec3& halfExtent, float dt,
                       bool* outGrounded = nullptr) const;

    // ---- brush-entity movers (func_door, etc.) ----------------------------

    // Direct swept-box trace against a single brush model by index.  `modelOffset`
    // is the brush model's current translation in raw map units (same space as
    // `start`/`end`).  We bring the trace into the model's local space by
    // subtracting the offset, then test every brush in the model.
    BspTraceResult traceModel(int modelIndex, const Vec3& modelOffset,
                              const Vec3& start, const Vec3& end,
                              const Vec3& halfExtent, int contentMask) const;

    // World trace plus an arbitrary set of translated brush models.  Handy for
    // moving doors: the world tree is tested once, then each model in the map
    // is tested at its current offset, and the closest hit wins.  The key is
    // the model index, the value is its current translation in raw map units.
    BspTraceResult worldCollisionWithModels(const Vec3& start, const Vec3& end,
                                            const Vec3& halfExtent, int contentMask,
                                            const std::unordered_map<int, Vec3>& modelOffsets) const;

    // Same as slideVelocity but collides against the world tree + the supplied
    // translated brush models.  `modelOffsets` maps modelIndex -> offset in raw
    // map units.  This lets func_door movers block the player when closed and
    // pass through when opened far enough.
    Vec3 slideVelocityWithModels(const Vec3& start, Vec3& velocity, const Vec3& halfExtent, float dt,
                                 const std::unordered_map<int, Vec3>& modelOffsets,
                                 bool* outGrounded = nullptr) const;

    // ---- brush model description (public so callers can inspect) -----------
    struct BspModel
    {
        Vec3 mins{0.f, 0.f, 0.f};
        Vec3 maxs{0.f, 0.f, 0.f};
        int firstBrush = 0;
        int numBrushes = 0;
        int firstFace = 0;
        int numFaces = 0;
    };

    // Access parsed brush models (model 0 is worldspawn, [1..] are entity models).
    const std::vector<BspModel>& bsp_models() const { return m_bspModels; }

    struct Entity; // defined below
    int model_index_from_entity(const Entity& e) const;

    // Point query (zero-size traceBox) along a ray — picking, line-of-sight
    // checks, "what wall is the crosshair on". `direction` need not be
    // normalized; `maxDistance` bounds the search (defaults to a generous
    // 8192 raw map units, ~256m).
    BspRayHit raycast(const Vec3& origin, const Vec3& direction, float maxDistance = 8192.f) const;

   
    // ---- entities (populated by AssetManager::load_bsp_collision) ---------
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

    // Inverse of remove_entities: drops every entity whose classname does
    // NOT match `keepClassname` (i.e. keeps only those that match). Useful
    // when you want to strip everything except one type. Returns how many
    // were removed.
    int remove_entities_except(const std::string& keepClassname);

private:
    Mesh* m_mesh;
    std::vector<Material*> m_materials;
 

    // ---- collision data (Quake 3 IBSP planes/nodes/leafs/brushes) --------
    struct BspPlane { Vec3 normal{0.f,0.f,0.f}; float dist = 0.f; };
    struct BspNode { int planeIndex = 0; int children[2] = {0, 0}; };
    struct BspLeaf { int firstLeafBrush = 0; int numLeafBrushes = 0; };
    struct BspBrushSide { int planeIndex = 0; };
    // `contents` is the brush's raw CONTENTS_* bitmask (BspContents::*),
    // not a precomputed bool — lets worldCollision() apply any mask at
    // trace time instead of baking one fixed "is this solid" decision in
    // at load time.
    struct BspBrush { int firstSide = 0; int numSides = 0; int contents = 0; };

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
                  int contentMask, BspTraceResult& result) const;
    void traceLeaf(int leaf, const Vec3& origStart, const Vec3& origEnd, const Vec3& halfExtent,
                  int contentMask, BspTraceResult& result) const;
    void traceModelBrushes(int modelIndex, const Vec3& localStart, const Vec3& localEnd,
                           const Vec3& halfExtent, int contentMask, BspTraceResult& result) const;
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

// ============================================================================
// BSP brush-entity movers (func_door, func_plat, func_rotating, ...) — kept
// in this same header/BSPLoader.cpp on purpose, not spread across new
// files: they're just as much "BSP stuff" as BspInstance itself, driven by
// the same Entity/BspModel data it already parses.
// ============================================================================

// Base class for a "live" mover: wraps one of BspInstance's raw Entity
// records (classname + keyValues, see BspInstance::entities()) plus the
// brush model it moves, and drives its own position (offset — this node's
// own local position under the owning BspInstance) every frame through
// Node3D's _update() hook — already ticked by Scene::update(dt) once this
// is a child in the scene graph. Subclasses (BspDoor now; BspPlatform/
// BspRotator/... later) implement the actual motion in on_update_mover();
// this base only holds what every mover needs in common plus small
// keyValue-parsing helpers (speed/lip/angle/wait/...).
//
// The visual mesh is held directly (get_mesh()/get_materials(), same shape
// as BspInstance's own) rather than as a MeshInstance child on purpose:
// per-model meshes (AssetManager::load_bsp_mesh's out_modelMeshes) are
// built in the SAME uv2-lightmap, no-tangent vertex format as the map's
// own worldspawn mesh, and only look right drawn through SceneRenderer's
// dedicated BSP shader (SceneRenderer::m_bspEntityInstances) — a regular
// MeshInstance goes through the generic dynamic-lit forward pass instead,
// which isn't set up for this format and rendered them solid black.
class BspEntityInstance : public Node3D
{
public:
    static constexpr NodeType ClassType = NT_BSPENTITYINSTANCE;

    explicit BspEntityInstance(const std::string& name = "BspEntity") : Node3D(name)
    {
        m_type = NT_BSPENTITYINSTANCE;
    }

    bool is_a(NodeType t) const override { return t == NT_BSPENTITYINSTANCE || Node3D::is_a(t); }

    // Wires this mover to the map + the specific brush entity/model it
    // drives. `visualMesh` (AssetManager::load_bsp_mesh's out_modelMeshes
    // out-param), if given, becomes this instance's own mesh (see
    // get_mesh()) — leave null for movers with no mesh of their own (e.g.
    // a pure logic/trigger entity).
    void init(BspInstance* owner, const BspInstance::Entity& entity, int modelIndex, Mesh* visualMesh);

    BspInstance* owner() const { return m_owner; }
    const BspInstance::Entity& entity() const { return m_entity; }
    int model_index() const { return m_modelIndex; }

    // Same shape as BspInstance's own get_mesh()/get_materials() — read by
    // SceneRenderer's BSP draw pass (see m_bspEntityInstances).
    Mesh* get_mesh() const { return m_mesh; }
    const std::vector<Material*>& get_materials() const { return m_materials; }

    // Current brush-model translation (raw map units — same space
    // BspInstance's own collision works in), ready to drop straight into
    // BspInstance::worldCollisionWithModels/slideVelocityWithModels's
    // modelOffsets map: offsets[mover->model_index()] = mover->offset().
    // Also this node's own local position (get_world_matrix() folds it in
    // for the render pass), so the two never disagree.
    const Vec3& offset() const { return m_offset; }

protected:
    void _update(float dt) override { on_update_mover(dt); }

    // Subclasses do their actual motion here — called every frame with dt,
    // expected to end by calling set_offset() with the new translation.
    virtual void on_update_mover(float dt) { (void)dt; }

    void set_offset(const Vec3& o);

    // Quake .map entity keys are plain strings (Entity::keyValues) —
    // typed-parse helpers every mover subclass ends up needing.
    float key_float(const char* key, float def) const;
    int key_int(const char* key, int def) const;

    BspInstance* m_owner = nullptr;
    BspInstance::Entity m_entity;
    int m_modelIndex = -1;
    Mesh* m_mesh = nullptr;
    std::vector<Material*> m_materials;
    Vec3 m_offset{0.f, 0.f, 0.f};
};

// func_door: slides open along its dominant horizontal axis (or straight
// up for tall/narrow "elevator-shaped" models — same heuristic Quake3
// mappers rely on), triggered by proximity to whatever set_activator()
// last reported. Ported from the coregl BSP demo's original Door
// struct/computeDoorOpenOffset (demos/bsp/main.cpp), now a proper mover
// class instead of a loose struct + free function.
class BspDoor : public BspEntityInstance
{
public:
    explicit BspDoor(const std::string& name = "BspDoor") : BspEntityInstance(name) {}

    // Call once after init() — reads the model's bounds (BspInstance::
    // bsp_models()[model_index()]) plus the "lip"/"angle"/"speed" keys to
    // work out how far and which way this door slides.
    void setup();

    // Node whose global position is read every frame (converted from world
    // render space into this door's own raw map-unit space via the owning
    // BspInstance's world matrix) to decide "is someone near enough to
    // open this door" — the player's camera, typically. Set once; no need
    // to keep pushing a position by hand every frame. Never called = stays
    // shut.
    void set_activator(Node3D* node) { m_activatorNode = node; }

    // Read-only properties — everything on_update_mover() decided this
    // door should do, for callers that want to react to it (sound/HUD/
    // trigger chains) instead of re-deriving it from raw entity keys.
    bool is_moving() const { return offset() != (is_open() ? m_openOffset : m_closedOffset); }
    bool is_open() const { return m_open; }
    const Vec3& open_offset() const { return m_openOffset; }
    const Vec3& closed_offset() const { return m_closedOffset; }
    float speed() const { return m_speed; }

    // Paired/grouped doors (double doors, a bank of leaves — anything that
    // should trigger together) share a "team" key in the .map, exactly
    // like real Quake3's own door-linking convention (see id's
    // g_utils.c/G_UseTargets — a team's proximity/touch check isn't
    // per-leaf, the whole team moves as one). Call once after every door
    // in the map is created and set up — groups them by that key and
    // cross-links each group, so being near ANY leaf opens all of them,
    // not just whichever one you're standing closest to.
    static void link_teams(const std::vector<BspDoor*>& doors);

protected:
    void on_update_mover(float dt) override;

private:
    // Pure proximity test against `localPos` (already converted into this
    // door's raw map-unit space) — no side effects, so teammates can query
    // each other's without stepping on their own m_open.
    bool near_activator(const Vec3& localPos) const;

    Vec3 m_closedOffset{0.f, 0.f, 0.f};
    Vec3 m_openOffset{0.f, 0.f, 0.f};
    float m_speed = 100.f;
    Node3D* m_activatorNode = nullptr;
    bool m_open = false;
    std::vector<BspDoor*> m_team; // other doors sharing this one's "team" key
};
