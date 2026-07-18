// Hoverjet Racing 

//
// Controls: UP/DOWN thrust forward/back, LEFT/RIGHT strafe/turn, SPACE
// boost, TAB free-fly camera (hold left mouse to look, WASD/QE to move),
// F10 gif, ESC quit.


#include "demo_app.hpp"
#include "demo_fly.hpp"
#include "HoverPhysics.hpp"
#include "Npc.hpp"
#include "TreePlacement.hpp"
#include "Waypoints.hpp"
#include "audio.hpp"
#include <scene/ViewportFit.hpp>
#include <coregl/gl_batch.hpp>
#include <scene/AssetManager.hpp>
#include <scene/ByteArray.hpp>
#include <scene/Camera3D.hpp>
#include <scene/Filesystem.hpp>
#include <scene/Material.hpp>
#include <scene/Mesh.hpp>
#include <scene/MeshCollision.hpp>
#include <scene/MeshInstance.hpp>
#include <scene/ParticleSystemNode.hpp>
#include <scene/RibbonTrailNode.hpp>
#include <scene/Scene.hpp>
#include <scene/SceneRenderer.hpp>
#include <scene/TerrainNode.hpp>
#include <scene/TreeSystemNode.hpp>
#include <scene/LensFlareNode.hpp>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <vector>

// + chase-cam tuning: everything about "where the camera sits relative to
// the ship" and "how eased it feels" lives here, one place, instead of the
// same magic numbers repeated at every call site. +
namespace CamTuning
{
constexpr float kDistance = 12.f;    // behind the ship
constexpr float kHeight = 9.f;       // above the ship
constexpr float kLookHeight = 2.f;   // vertical offset of the look-at target
constexpr float kPosFrequency = 3.5f; // Hz — how fast the rig itself eases in (lower = floatier)
constexpr float kLookFrequency = 6.f; // Hz — how fast the look target eases in (keep > kPosFrequency
                                      // so the view stays on the ship while the rig is still catching up)
constexpr float kDamping = 1.f;      // 1 = critically damped (no overshoot/bounce)
} // namespace CamTuning

// + ship-handling tuning: HoverJet (HoverPhysics.hpp) keeps these as
// per-instance fields with defaults (so multiple NPCs can each fly
// differently), but for the single player ship here it's more convenient
// to have every knob in one visible place next to CamTuning — assigned
// onto `jet` right after construction, below. +
namespace ShipTuning
{
constexpr float kThrustAccel = 55.f;  // forward accel (units/s^2)
constexpr float kBoostMul = 2.4f;     // SPACE multiplies kThrustAccel by this
constexpr float kReverseAccel = 20.f; // DOWN
constexpr float kStrafeAccel = 34.f;  // LEFT/RIGHT turning force
constexpr float kGravity = 14.f;
constexpr float kLinearDrag = 0.6f;  // higher = lower top speed at the same thrust
constexpr float kHoverHeight = 2.f;  // clearance kept above the terrain
constexpr float kMaxSpeed = 150.f;   // hard cap; equilibrium speed is thrustAccel/linearDrag,
                                     // this only bites if that's raised past it
constexpr float kStickLength = 5.f; // body-nose distance — shorter turns sharper (see HoverPhysics.hpp)
} // namespace ShipTuning

struct DampedSpring3
{
    Vec3 value, velocity;
    void seed(const Vec3& v)
    {
        value = v;
        velocity = Vec3(0.f, 0.f, 0.f);
    }
    void update(const Vec3& target, float dt, float frequency, float damping)
    {
        Vec3 accel = (target - value) * (frequency * frequency) - velocity * (2.f * damping * frequency);
        velocity += accel * dt;
        value += velocity * dt;
    }
};

int main(int argc, char** argv)
{
    const int maxFrames = (argc > 1) ? atoi(argv[1]) : 0;

    DemoApp app;
    if (!app.Create("coregl - hoverjet racing")) return 1;
    printf("controls: UP/DOWN thrust | LEFT/RIGHT turn | SPACE boost | TAB free cam | R reset | "
          "F10 gif | ESC\n");

    SceneRenderer renderer;
    if (!renderer.init() || !renderer.enable_shadows(4, 2048, 400.f))
    {
        fprintf(stderr, "renderer init failed\n");
        app.Destroy();
        return 1;
    }
    renderer.set_light_dir(Vec3(0.45f, -0.7f, -0.35f));
    renderer.set_sky_enabled(true);
    renderer.enable_post();
    renderer.set_bloom_params(2.6f, 0.7f);

    Scene scene;
    assets::AssetManager& assets = assets::AssetManager::instance();

    // ── audio: loaded via fs::getFilesystem() (same multi-root search as
    // every other asset) into memory, then decoded from that buffer —
    // audio::Engine's own createSfx/createMusic take a raw path and hand
    // it straight to miniaudio's file I/O, which only ever sees the
    // process's cwd, so it wouldn't resolve when run from build/games/
    // hoverjet/ the way models/textures/track.txt already do. ──
    auto loadSound = [&](audio::Engine& eng, const char* path, bool music) -> audio::Engine::SoundId {
        scene::ByteArray bytes;
        if (!fs::getFilesystem().readFile(path, bytes))
        {
            fprintf(stderr, "audio: could not read '%s'\n", path);
            return 0;
        }
        return music ? eng.createMusicFromMemory(bytes.data(), bytes.size())
                    : eng.createSfxFromMemory(bytes.data(), bytes.size());
    };
    audio::Engine audioEngine;
    bool audioReady = audioEngine.init();
    audio::Engine::SoundId engineSnd = 0, hitSnd = 0, musicSnd = 0;
    audio::Engine::Handle engineVoice = 0;
    if (audioReady)
    {
        // engine hum: a *looping SFX*, not music — playMusic() only tracks
        // one active handle, so using it here would fight the actual
        // soundtrack every time either one started playing (see
        // playSfxLoop's own comment in audio.hpp).
        engineSnd = loadSound(audioEngine, "games/hoverjet/assets/jetengine.wav", false);
        hitSnd = loadSound(audioEngine, "games/hoverjet/assets/crash.wav", false);
        musicSnd = loadSound(audioEngine, "games/hoverjet/assets/tetuano.mp3", true);
        printf("DEBUG audio ids: engine=%d hit=%d music=%d\n", engineSnd, hitSnd, musicSnd);
        if (engineSnd) engineVoice = audioEngine.playSfxLoop(engineSnd, 0.f);
        if (musicSnd) audioEngine.playMusic(musicSnd, true, 0.5f);
    }
    else
        fprintf(stderr, "audio: init failed, running without sound\n");
    float hitSoundCooldown = 0.f; // shared across all ship pairs — see the collision block below

    TerrainNode* terrain = scene.root().create_child<TerrainNode>("terrain");
  
    const float terrainSize = 1800.f, terrainHeight = 120.f;
    if (!terrain->load_heightmap("games/hoverjet/assets/terrainE.png", terrainSize, terrainHeight,
                                 terrainSize))
    {
        std::vector<float> hs((size_t)256 * 256);
        for (int z = 0; z < 256; ++z)
            for (int x = 0; x < 256; ++x)
                hs[(size_t)z * 256 + x] =
                    0.5f + 0.3f * sinf(x * 0.05f) * cosf(z * 0.06f) + 0.15f * sinf(x * 0.13f + z * 0.09f);
        terrain->build(hs.data(), 256, terrainSize, terrainHeight, terrainSize);
    }
    Material* groundMat = scene.create_material();
   
    groundMat->diffuse = assets.loadTexture("terrainmap", "games/hoverjet/assets/terrainE.jpg");
    //groundMat->diffuse = assets.loadTexture("terrainmap", "games/hoverjet/assets/terrainEPoints.jpg");

    

    
    groundMat->detail = assets.loadTexture("terrainmapB", "games/hoverjet/assets/detailmap3.jpg");
    groundMat->detail_scale = 60.f;
    terrain->set_material(groundMat);

    // ── trees: static billboard batch (TreeSystemNode). Placed from
    // terrainTree.png (red-painted forest patches, same idea as the old
    // waypoint image — see TreePlacement.hpp for exactly how blobs turn
    // into tree counts/positions) instead of a blind random scatter, which
    // is what put trees mid-air on cliffs/the track with no regard for
    // what a human would actually put there. trees1.png is the original
    // Lua demo's tree sheet — a real 2x2 atlas of 4 distinct cutout
    // sprites, exactly what set_atlas_grid() is for. ──
    TreeSystemNode* trees = scene.root().create_child<TreeSystemNode>("trees", 3000);
    trees->texture = assets.loadTexture("trees1", "games/hoverjet/assets/trees1.png");
    trees->set_atlas_grid(2, 2);
    trees->set_cutout(0.35f);
    {
        std::vector<Vec3> spots =
            treeplacement::load_from_image("games/hoverjet/assets/terrainTree.png", *terrain,
                                           terrainSize);
        std::mt19937 rng(1234);
        std::uniform_real_distribution<float> u(0.f, 1.f);
        for (const Vec3& p : spots)
        {
            float size = 6.f + u(rng) * 10.f;
            int col = (int)(u(rng) * 2.f), row = (int)(u(rng) * 2.f);
            trees->addTree(p, Vec2(size * 0.7f, size), col, row);
        }
        if (spots.empty())
            fprintf(stderr, "trees: no terrainTree.png markers found, no trees placed\n");
    }
    trees->build();


    std::vector<Material*> shipMats;
    Mesh* shipMesh =
        assets.load_3ds_mesh("astro1", "games/hoverjet/assets/astro1.3ds", shipMats);
    if (!shipMesh)
    {
        fprintf(stderr, "ship mesh load failed (run from repo root)\n");
        app.Destroy();
        return 1;
    }
    MeshInstance* ship = scene.root().create_child<MeshInstance>("ship");
    ship->set_mesh(shipMesh);
    ship->set_materials(shipMats);
    ship->set_scale(0.4f);

    // ── engine exhaust: astro1.3ds is a twin-engine hull, so two emitters
    // (one per nacelle) instead of one dead-centered. Children of the ship,
    // so their own local transform carries the ship's rotation — local -Z
    // is the tail direction (see the look_at comment above: local +Z is
    // the nose). x offset guessed as a quarter of the hull's width either
    // side of center; y/z at the mesh's own local -Z/lower extent so they
    // sit at the back of the hull whatever astro1.3ds' actual proportions
    // are. ──
    auto makeExhaust = [&](const char* name, float xOffset) {
        ParticleSystemNode* fx = ship->create_child<ParticleSystemNode>(name, 200);
        fx->texture = assets.loadTexture("particle_glow", "assets/textures/light.jpg");
        fx->blend = ParticleBlendMode::Additive;
        fx->set_position(xOffset, shipMesh->bounds().min.y * 0.3f, shipMesh->bounds().min.z+1.5f);
        fx->setContinuous(0.f) // rate driven from the input state each frame, see below
            ->setShapeCircle(0.25f)
            ->setEmissionDirection(Vec3(0.f, 0.f, -1.f))
            ->setSpreadAngle(6.f)
            ->setSpeed(6.f, 10.f)
            ->setLifetime(0.25f, 0.4f)
            ->setSize(Vec2(0.5f, 0.5f), Vec2(0.08f, 0.08f))
            ->setColor(Vec4(1.f, 0.85f, 0.4f, 0.9f), Vec4(1.f, 0.25f, 0.05f, 0.f));
        return fx;
    };
    float engineSpread = (shipMesh->bounds().max.x - shipMesh->bounds().min.x) * 0.30f;
    ParticleSystemNode* exhaustL = makeExhaust("exhaust_l", -engineSpread);
    ParticleSystemNode* exhaustR = makeExhaust("exhaust_r", engineSpread);

    // ── wingtip vapor trails: sharp turns light up a ribbon "sheet" from
    // each wing, like a jet pulling condensation in a hard bank. Each wing
    // is a blade chain (see sinbad_beam's sword trails) between a front
    // and a back point on that wing, not a single trailing point — that
    // fills the swept quad along the wing's own edge instead of drawing a
    // thin line trailing off one spot. All 4 points are plain Node3D
    // children of the ship, so they follow its transform automatically
    // (no per-frame position update needed), same trick as the exhaust
    // emitters above. x offset is the hull's actual half-width (the
    // wingtip), not the quarter-width the engines use.
    float wingChord = (shipMesh->bounds().max.z - shipMesh->bounds().min.z) * 0.12f;
    Node3D* wingLFront = ship->create_child<Node3D>("wingtip_l_front");
    wingLFront->set_position(shipMesh->bounds().min.x, 0.f, wingChord);
    Node3D* wingLBack = ship->create_child<Node3D>("wingtip_l_back");
    wingLBack->set_position(shipMesh->bounds().min.x, 0.f, -wingChord);
    Node3D* wingRFront = ship->create_child<Node3D>("wingtip_r_front");
    wingRFront->set_position(shipMesh->bounds().max.x, 0.f, wingChord);
    Node3D* wingRBack = ship->create_child<Node3D>("wingtip_r_back");
    wingRBack->set_position(shipMesh->bounds().max.x, 0.f, -wingChord);

    RibbonTrailNode* wingTrails = scene.root().create_child<RibbonTrailNode>("wing_trails", 2, 40);
    wingTrails->setTrailLength(0.9f);
    wingTrails->blend = ParticleBlendMode::Additive;
    wingTrails->depthTest = true;
    // light blue right off the wing, whitening out as it fades — an icy
    // contrail look instead of a flat single-tone streak
    wingTrails->addBladeChain(wingLFront, wingLBack, Vec4(0.55f, 0.8f, 1.f, 0.7f),
                              Vec4(1.f, 1.f, 1.f, 0.f), 0.5f, 1.f, 0.f);
    wingTrails->addBladeChain(wingRFront, wingRBack, Vec4(0.55f, 0.8f, 1.f, 0.7f),
                              Vec4(1.f, 1.f, 1.f, 0.f), 0.5f, 1.f, 0.f);
    wingTrails->setEmitting(false); // only while turning hard — see the main loop

    // ── collision sparks: one reusable world-space emitter, repositioned
    // to the contact point and burst-fired on every hit — cheaper than a
    // fresh node per collision, and collisions are frequent/short enough
    // that a single shared emitter never needs two bursts alive with
    // visibly different origins at once. ──
    ParticleSystemNode* collisionSparks = scene.root().create_child<ParticleSystemNode>("hit_sparks", 64);
    collisionSparks->texture = assets.loadTexture("particle_glow", "assets/textures/light.jpg");
    collisionSparks->blend = ParticleBlendMode::Additive;
    collisionSparks->setShapeSphere(0.6f)
        ->setSpeed(4.f, 12.f)
        ->setLifetime(0.2f, 0.45f)
        ->setSize(Vec2(0.35f, 0.35f), Vec2(0.05f, 0.05f))
        ->setColor(Vec4(1.f, 0.9f, 0.6f, 1.f), Vec4(1.f, 0.3f, 0.05f, 0.f))
        ->setGravity(Vec3(0.f, -9.f, 0.f));
    // rad/s of actual heading change (not just LEFT/RIGHT held) needed
    // before the trails light up — tune this, not the key state, since
    // holding the turn key at low speed barely turns the nose at all
    const float kTrailTurnRateThreshold = 0.6f;
    Vec3 trailPrevForward(0.f, 0.f, -1.f); // set properly once jet.init() runs below

    // + checkpoint waypoints: hand-placed in the in-game editor (TAB free
    // cam, N place, LEFT/RIGHT aim, BACKSPACE undo, DELETE clear all, F5
    // save — see the editor block after the main loop starts) — exact
    // positions AND facing, in the order you placed them. No fallback: an
    // empty/missing track just means no gates yet, build the real one.
    const float kGateMajorRadius = 10.f, kGateMinorRadius = 1.5f;
    const float kLoopClearance = 8.f; // just above normal hover altitude (hoverHeight ~2)
    const float kTriggerRadius = kGateMajorRadius + kGateMinorRadius * 3.f;
    // if the ship ends up this far from the gate it's chasing (fell off the
    // track, wedged somewhere, whatever), snap it back to the gate it last
    // passed instead of leaving it stranded
    const float kRespawnDistance = 400.f;

    const char* kTrackPath = "games/hoverjet/assets/track.txt";
    std::vector<Vec3> gatePos;
    std::vector<float> gateYaw;
    waypoints::load_track(kTrackPath, gatePos, gateYaw);
    const int kGateCount = (int)gatePos.size();

    Mesh* gateMesh = assets.createTorus("gate", kGateMajorRadius, kGateMinorRadius);
    Material* gateActiveMat = scene.create_material(1.f, 0.85f, 0.15f);
    gateActiveMat->unlit = true;
    Material* gatePassedMat = scene.create_material(1.f, 0.3f, 0.05f);
    gatePassedMat->unlit = true;

    std::vector<MeshInstance*> gates;
    for (int i = 0; i < kGateCount; ++i)
    {
        MeshInstance* gate = scene.root().create_child<MeshInstance>("gate");
        gate->set_mesh(gateMesh);
        gate->set_material(i == 0 ? gateActiveMat : gatePassedMat);
        gate->set_position(gatePos[(size_t)i]);
        gate->set_rotation(Quaternion::FromAxisAngle(Vec3(0.f, 1.f, 0.f), gateYaw[(size_t)i]));
        gates.push_back(gate);
    }
    int nextGate = 0;
    int laps = 0;

    // + flight model: spawns at the first waypoint (facing the second), or
    // the map center if there's no track yet — build one with TAB+N+F5 +
    HoverJet jet;
    jet.thrustAccel = ShipTuning::kThrustAccel;
    jet.boostMul = ShipTuning::kBoostMul;
    jet.reverseAccel = ShipTuning::kReverseAccel;
    jet.strafeAccel = ShipTuning::kStrafeAccel;
    jet.gravity = ShipTuning::kGravity;
    jet.linearDrag = ShipTuning::kLinearDrag;
    jet.hoverHeight = ShipTuning::kHoverHeight;
    jet.maxSpeed = ShipTuning::kMaxSpeed;
    {
        Vec3 startPos = !gatePos.empty()
            ? gatePos[0]
            : terrain->get_position() + Vec3(terrainSize * 0.5f, 0.f, terrainSize * 0.5f);
        startPos.y = terrain->height_at(startPos.x, startPos.z) + jet.hoverHeight + 2.f;
        jet.init(startPos, ShipTuning::kStickLength);
        // face the second waypoint so it doesn't spawn staring backward
        if (gatePos.size() >= 2)
        {
            Vec3 aim = (gatePos[1] - gatePos[0]);
            aim.y = 0.f;
            if (aim.length() > 1e-3f)
            {
                aim = aim.normalized();
                jet.nose = jet.body + aim * jet.stickLength;
                jet.nosePrev = jet.nose;
            }
        }
    }
    trailPrevForward = jet.forward();

    // ── NPCs: 2 AI ships, same track, spawned right beside the player at
    // gate 0 (not scattered around the course) — see Npc.hpp. ──
    std::vector<Npc> npcs;
    std::vector<Vec3> npcSpawnOffset; // parallel to npcs — reused by resetGame() below
    if (kGateCount >= 2)
    {
        Vec3 aimDir = gatePos[1] - gatePos[0];
        aimDir.y = 0.f;
        aimDir = aimDir.length() > 1e-3f ? aimDir.normalized() : Vec3(0.f, 0.f, -1.f);
        Vec3 lateral(-aimDir.z, 0.f, aimDir.x); // "right" of the start line
        const struct
        {
            const char* name;
            const char* file;
            float lateralOffset;
        } npcDefs[2] = {
            {"astro2", "games/hoverjet/assets/astro2.3ds", -8.f},
            {"astro3", "games/hoverjet/assets/astro3.3ds", 8.f},
        };
        for (const auto& def : npcDefs)
        {
            // spawn() places it at gatePos[0] then aims at gatePos[1]; nudge
            // sideways afterward so it lines up next to the player instead
            // of on top of it, same as a racing-game starting grid
            Npc npc = Npc::spawn(scene, assets, def.name, def.file, *terrain, gatePos, 0, 1,
                                 ShipTuning::kThrustAccel * 0.75f, ShipTuning::kStrafeAccel,
                                 ShipTuning::kLinearDrag, ShipTuning::kHoverHeight, ShipTuning::kMaxSpeed,
                                 ShipTuning::kStickLength);
            if (!npc.mesh) continue;
            Vec3 offset = lateral * def.lateralOffset;
            npc.jet.body += offset;
            npc.jet.bodyPrev += offset;
            npc.jet.nose += offset;
            npc.jet.nosePrev += offset;
            npcs.push_back(npc);
            npcSpawnOffset.push_back(offset);
        }
    }

    // ── real mesh collision (box-tree + triangle-triangle, see
    // scene/MeshCollision.hpp) — used below to upgrade the collision
    // sparks' spawn point from "midpoint of the two ships' centers" to the
    // actual hull contact point. resolveShipCollision's sphere test still
    // drives the physical push-apart (cheap, already tuned); this only
    // runs when that sphere test already found a hit, so it's not paying
    // for a per-pair box-tree test every frame regardless of distance. ──
    CollisionRegistry meshCollision;
    int shipCollider = meshCollision.add(shipMesh, ship);
    std::vector<int> npcColliders;
    for (Npc& npc : npcs) npcColliders.push_back(meshCollision.add(npc.mesh->get_mesh(), npc.mesh));


    // two independent cameras, each keeping its own state — switching
    // modes is just scene.set_active_camera(), not reprogramming one
    // camera's transform from two different code paths. The inactive one
    // simply isn't updated, so it's exactly where you left it next time
    // you switch back (no reseed-on-toggle hacks needed).
    Camera3D* chaseCam = scene.root().create_child<Camera3D>("chase");
    chaseCam->set_perspective(65.f, 0.1f, 3000.f);
    Camera3D* worldCam = scene.root().create_child<Camera3D>("world");
    worldCam->set_perspective(70.f, 0.1f, 3000.f);

    bool freeCam = false; // TAB: game (chaseCam) <-> world (worldCam, free fly)
    FlyCam fly;
    fly.speed = 220.f; // this map is ~1800 units wide; a walking-speed flycam would take forever
    float camFov = 65.f; // eased target for chaseCam's speed-based FOV kick below

    // ── viewport fit: renders at a fixed 1280x720 virtual resolution
    // regardless of the actual window size — F6 cycles Letterbox (bars,
    // default) / Stretch (distorts to fill) / Crop (fills, overflow
    // clipped). See scene/ViewportFit.hpp + SceneRenderer::render_fit(). ──
    const int kVirtualW = 1280, kVirtualH = 720;
    ViewportFitMode viewportMode = ViewportFitMode::Letterbox;
    bool bloomOn = true; // F7 toggles

    // spring-damped chase cam: position lags/eases behind the desired chase
    // pose, look target lags a bit less (a snappier look keeps the ship
    // framed even while the rig itself is still easing into place)
    DampedSpring3 camPosSpring, camLookSpring;
    {
        Vec3 startShipPos = jet.position();
        Vec3 startFwd = jet.forward();
        Vec3 startPos = startShipPos - startFwd * CamTuning::kDistance + Vec3(0.f, CamTuning::kHeight, 0.f);
        camPosSpring.seed(startPos);
        camLookSpring.seed(startShipPos + Vec3(0.f, CamTuning::kLookHeight, 0.f));
        // worldCam starts at the same spot so the first TAB press doesn't teleport
        worldCam->set_position(startPos);
        worldCam->look_at(startShipPos);
        Vec3 e = worldCam->get_euler();
        fly.pitch = e.x;
        fly.yaw = e.y;
    }

    // 3, 2, 1, GO: gameplay (jet/NPC physics, gate triggers, ship-vs-ship
    // collision) stays frozen until this reaches 0 — see the "racing" gate
    // used throughout the main loop below. Restarted by resetGame().
    float raceCountdown = 4.f;

    // ── full reset (R, and the initial 3-2-1-GO above): player + every NPC
    // back to the starting grid, gates cleared back to "not passed",
    // laps/progress zeroed, chase cam snapped to the new pose instead of
    // sliding from wherever it was (same seed() trick used above). Doesn't
    // touch ship/npc.mesh transforms directly — the main loop already
    // syncs those from jet.position()/forward() every frame regardless, so
    // next frame's normal sync picks the reset pose up with no extra code.
    auto resetGame = [&]()
    {
        raceCountdown = 4.f;
        nextGate = 0;
        laps = 0;
        for (int i = 0; i < kGateCount; ++i)
            gates[(size_t)i]->set_material(i == 0 ? gateActiveMat : gatePassedMat);

        if (kGateCount > 0)
        {
            Vec3 startPos = gatePos[0];
            startPos.y = terrain->height_at(startPos.x, startPos.z) + jet.hoverHeight + 2.f;
            jet.init(startPos, ShipTuning::kStickLength);
            if (kGateCount >= 2)
            {
                Vec3 aim = gatePos[1] - gatePos[0];
                aim.y = 0.f;
                if (aim.length() > 1e-3f)
                {
                    aim = aim.normalized();
                    jet.nose = jet.body + aim * jet.stickLength;
                    jet.nosePrev = jet.nose;
                }
            }
        }

        for (size_t k = 0; k < npcs.size(); ++k)
        {
            Npc& npc = npcs[k];
            Vec3 startPos = gatePos[0];
            startPos.y = terrain->height_at(startPos.x, startPos.z) + npc.jet.hoverHeight + 2.f;
            npc.jet.init(startPos, ShipTuning::kStickLength);
            Vec3 aim = gatePos[1] - gatePos[0];
            aim.y = 0.f;
            if (aim.length() > 1e-3f)
            {
                aim = aim.normalized();
                npc.jet.nose = npc.jet.body + aim * npc.jet.stickLength;
                npc.jet.nosePrev = npc.jet.nose;
            }
            npc.jet.body += npcSpawnOffset[k];
            npc.jet.bodyPrev += npcSpawnOffset[k];
            npc.jet.nose += npcSpawnOffset[k];
            npc.jet.nosePrev += npcSpawnOffset[k];
            npc.targetGate = 1;
        }

        Vec3 startShipPos = jet.position();
        Vec3 startFwd = jet.forward();
        Vec3 camPos =
            startShipPos - startFwd * CamTuning::kDistance + Vec3(0.f, CamTuning::kHeight, 0.f);
        camPosSpring.seed(camPos);
        camLookSpring.seed(startShipPos + Vec3(0.f, CamTuning::kLookHeight, 0.f));

        printf("game reset\n");
    };

    // + track editor: only active in free-cam mode. Aim with the mouse
    // (a live sphere+gate preview shows the pick point and current facing
    // — see the per-frame block below), N commits it, LEFT/RIGHT rotates
    // the *pending* facing before you commit, BACKSPACE undoes the last
    // commit, F5 writes everything to kTrackPath. Seeded from whatever
    // track.txt already had (the `gates` built above), so re-opening the
    // editor continues the existing track instead of starting over — the
    // same MeshInstances are reused as edit markers, no parallel list. +
    std::vector<Vec3> editPos = gatePos;
    std::vector<float> editYaw = gateYaw;
    std::vector<MeshInstance*> editMarkers = gates;
    // new points start facing the same way as the last placed one, not 0 —
    // you almost always want consecutive gates roughly aligned
    float pendingYaw = editYaw.empty() ? 0.f : editYaw.back();
    bool haveCursorHit = false;
    Vec3 cursorHit(0.f, 0.f, 0.f);

    Mesh* pickSphereMesh = assets.createSphere("pick_sphere", 3.f, 8, 12);
    Material* pickMat = scene.create_material(0.2f, 1.f, 1.f);
    pickMat->unlit = true;
    MeshInstance* pickMarker = scene.root().create_child<MeshInstance>("pick_marker");
    pickMarker->set_mesh(pickSphereMesh);
    pickMarker->set_material(pickMat);
    pickMarker->visible = false;

    Material* previewGateMat = scene.create_material(0.2f, 1.f, 0.4f);
    previewGateMat->unlit = true;
    MeshInstance* previewGate = scene.root().create_child<MeshInstance>("preview_gate");
    previewGate->set_mesh(gateMesh);
    previewGate->set_material(previewGateMat);
    previewGate->visible = false;

    Material* placedMat = scene.create_material(1.f, 0.55f, 0.1f);
    placedMat->unlit = true;

    LensFlareNode* flare = scene.root().create_child<LensFlareNode>("sun_flare");

    scene.set_active_camera(chaseCam);
    scene.ready();

    gl::Batch hud;
    hud.Init();

    gl::u64 lastTicks = SDL_GetPerformanceCounter();
    const gl::u64 freq = SDL_GetPerformanceFrequency();

    int frame = 0;
    bool running = true;
    while (running)
    {
        SDL_Event ev;
        while (SDL_PollEvent(&ev))
        {
            if (ev.type == SDL_QUIT) running = false;
            if (ev.type == SDL_KEYDOWN || ev.type == SDL_KEYUP)
            {
                bool down = ev.type == SDL_KEYDOWN;
                SDL_Scancode sc = ev.key.keysym.scancode;
                if (down && sc == SDL_SCANCODE_ESCAPE) running = false;
                if (down && sc == SDL_SCANCODE_F10)
                {
                    int gw, gh;
                    app.DrawableSize(&gw, &gh);
                    app.gif.Toggle(gw, gh);
                }
                if (down && sc == SDL_SCANCODE_F6)
                {
                    viewportMode = viewportMode == ViewportFitMode::Letterbox ? ViewportFitMode::Stretch
                                  : viewportMode == ViewportFitMode::Stretch  ? ViewportFitMode::Crop
                                                                              : ViewportFitMode::Letterbox;
                    const char* names[] = {"Stretch", "Letterbox", "Crop"};
                    printf("viewport: %s\n", names[(int)viewportMode]);
                }
                if (down && sc == SDL_SCANCODE_F7)
                {
                    bloomOn = !bloomOn;
                    renderer.set_bloom_enabled(bloomOn);
                    printf("bloom: %s\n", bloomOn ? "on" : "off");
                }
                if (down && sc == SDL_SCANCODE_R) resetGame();
                if (down && sc == SDL_SCANCODE_TAB)
                {
                    freeCam = !freeCam;
                    scene.set_active_camera(freeCam ? worldCam : chaseCam);
                }
                if (freeCam)
                {
                    // + track editor keys (see the editor state block above) +
                    if (down && sc == SDL_SCANCODE_N && haveCursorHit)
                    {
                        editPos.push_back(cursorHit);
                        editYaw.push_back(pendingYaw);
                        MeshInstance* marker = scene.root().create_child<MeshInstance>("waypoint");
                        marker->set_mesh(gateMesh);
                        marker->set_material(placedMat);
                        marker->set_position(cursorHit);
                        marker->set_rotation(Quaternion::FromAxisAngle(Vec3(0.f, 1.f, 0.f), pendingYaw));
                        editMarkers.push_back(marker);
                        printf("waypoint editor: placed #%d (%d total)\n", (int)editPos.size(),
                              (int)editPos.size());
                    }
                    else if (down && sc == SDL_SCANCODE_BACKSPACE && !editPos.empty())
                    {
                        editPos.pop_back();
                        editYaw.pop_back();
                        editMarkers.back()->visible = false; // leaked, not worth a real removal API for a demo tool
                        editMarkers.pop_back();
                        printf("waypoint editor: undo (%d left)\n", (int)editPos.size());
                    }
                    else if (down && sc == SDL_SCANCODE_F5) 
                    {
                        bool ok = waypoints::save_track(kTrackPath, editPos, editYaw);
                        printf("waypoint editor: %s %d point(s) to %s\n", ok ? "saved" : "FAILED to save",
                              (int)editPos.size(), kTrackPath);
                    }
                    else if (down && sc == SDL_SCANCODE_DELETE)
                    {
                        for (MeshInstance* m : editMarkers) m->visible = false; // leaked, see BACKSPACE above
                        editMarkers.clear();
                        editPos.clear();
                        editYaw.clear();
                        printf("waypoint editor: cleared all\n");
                    }
                }
                else
                {
                    jet.handle_key(sc, down);
                }
            }
            fly.handle(ev); // only updates yaw/pitch/looking state; harmless outside free-cam mode
        }
        if (!running) break;

        gl::u64 now = SDL_GetPerformanceCounter();
        float dt = (float)((double)(now - lastTicks) / (double)freq);
        lastTicks = now;
        if (dt > 0.1f) dt = 0.1f;

        // computed once up front (not down by the render call) because the
        // free-cam mouse-picking block below also needs it: mouse
        // coordinates are in real window pixels, but content only actually
        // draws inside `vp` once letterboxed/cropped, so picking has to
        // convert into that sub-rect too or clicks drift off-target
        // whenever the window aspect doesn't match kVirtualW/kVirtualH.
        int windowW, windowH;
        app.DrawableSize(&windowW, &windowH);
        ViewportRect vp = compute_viewport_fit(windowW, windowH, kVirtualW, kVirtualH, viewportMode);

        // 3, 2, 1, GO: gameplay stays frozen (no physics/AI/collision)
        // until the countdown reaches 0 — see resetGame() above, which
        // restarts it. Real elapsed time, not sim time, so it can't be
        // affected by whatever's happening in the (frozen) simulation.
        if (raceCountdown > 0.f) raceCountdown -= dt;
        bool racing = raceCountdown <= 0.f;

        if (racing) jet.update(dt, terrain);

        Vec3 shipPos = jet.position();
        ship->set_position(shipPos);
        ship->look_at(shipPos - jet.forward());
        {
            float rate = (racing && jet.engineForward) ? (jet.boost ? 260.f : 140.f) : 0.f;
            exhaustL->setContinuous(rate);
            exhaustR->setContinuous(rate);
        }
        // wingtip vapor trails: only while the nose is actually swinging
        // fast, not just while LEFT/RIGHT is held — holding the key at low
        // speed or against the stick constraint barely turns the ship
        {
            Vec3 curForward = jet.forward();
            float cosAngle = Vec3::Dot(trailPrevForward, curForward);
            cosAngle = cosAngle < -1.f ? -1.f : (cosAngle > 1.f ? 1.f : cosAngle);
            float turnRate = dt > 0.f ? acosf(cosAngle) / dt : 0.f;
            wingTrails->setEmitting(turnRate > kTrailTurnRateThreshold);
            trailPrevForward = curForward;
        }

        // ── engine hum: volume/pitch tied to thrust + current speed, one
        // looping voice started once at setup (see loadSound above) ──
        if (audioReady && engineVoice)
        {
            float speedFrac = jet.speed(dt) / ShipTuning::kMaxSpeed;
            if (speedFrac > 1.f) speedFrac = 1.f;
            float targetVol = jet.engineForward ? (jet.boost ? 0.9f : 0.6f) : 0.25f;
            audioEngine.setVolume(engineVoice, targetVol);
            audioEngine.setPitch(engineVoice, 0.8f + speedFrac * 0.7f);
        }

        // ── checkpoint gate: sphere trigger around the next gate's center;
        // too far the other way instead respawns at the last gate passed ──
        if (racing && kGateCount > 0)
        {
            float distToNext = (shipPos - gatePos[(size_t)nextGate]).length();
            if (distToNext < kTriggerRadius)
            {
                gates[(size_t)nextGate]->set_material(gatePassedMat);
                nextGate = (nextGate + 1) % kGateCount;
                if (nextGate == 0) ++laps;
                gates[(size_t)nextGate]->set_material(gateActiveMat);
            }
            else if (distToNext > kRespawnDistance)
            {
                int prevGate = (nextGate - 1 + kGateCount) % kGateCount;
                Vec3 respawnPos = gatePos[(size_t)prevGate];
                respawnPos.y = terrain->height_at(respawnPos.x, respawnPos.z) + jet.hoverHeight + 2.f;
                jet.init(respawnPos, ShipTuning::kStickLength);
                Vec3 aim = gatePos[(size_t)nextGate] - gatePos[(size_t)prevGate];
                aim.y = 0.f;
                if (aim.length() > 1e-3f)
                {
                    aim = aim.normalized();
                    jet.nose = jet.body + aim * jet.stickLength;
                    jet.nosePrev = jet.nose;
                }
                printf("respawned at gate %d (was %.0f units from gate %d)\n", prevGate, distToNext,
                      nextGate);
                // this frame's ship/camera/HUD were already computed from
                // the stale position above — refresh so nothing lags a frame
                shipPos = jet.position();
                ship->set_position(shipPos);
                ship->look_at(shipPos - jet.forward());
            }
        }

        if (racing)
            for (Npc& npc : npcs) npc.update(dt, terrain, gatePos, kTriggerRadius);

        // ── ship-vs-ship collision: player-vs-NPC and NPC-vs-NPC, keeps
        // them from flying through each other (see HoverPhysics.hpp) ──
        const float kShipCollisionRadius = 2.5f;
        bool anyHit = false;
        if (racing)
        {
            // falls back to the midpoint of the two sphere centers if the
            // mesh test somehow misses (spheres can overlap slightly
            // before hulls actually touch) — always spawns sparks
            // somewhere sensible
            for (size_t k = 0; k < npcs.size(); ++k)
            {
                Npc& npc = npcs[k];
                if (resolveShipCollision(jet, npc.jet, kShipCollisionRadius))
                {
                    anyHit = true;
                    ContactInfo info;
                    Vec3 pos = meshCollision.test(shipCollider, npcColliders[k], info)
                                  ? info.point
                                  : (jet.position() + npc.jet.position()) * 0.5f;
                    collisionSparks->set_position(pos);
                    collisionSparks->emitBurst(20);
                }
            }
            for (size_t i = 0; i < npcs.size(); ++i)
                for (size_t j = i + 1; j < npcs.size(); ++j)
                {
                    if (resolveShipCollision(npcs[i].jet, npcs[j].jet, kShipCollisionRadius))
                    {
                        anyHit = true;
                        ContactInfo info;
                        Vec3 pos = meshCollision.test(npcColliders[i], npcColliders[j], info)
                                      ? info.point
                                      : (npcs[i].jet.position() + npcs[j].jet.position()) * 0.5f;
                        collisionSparks->set_position(pos);
                        collisionSparks->emitBurst(20);
                    }
                }
        }

        hitSoundCooldown -= dt;
        if (anyHit && audioReady && hitSnd && hitSoundCooldown <= 0.f)
        {
            audio::Engine::Handle h =
                audioEngine.playSfx(hitSnd, 0.8f, 0.9f + (float)rand() / (float)RAND_MAX * 0.2f);
           // printf("DEBUG hit sound: handle=%d\n", h);
            hitSoundCooldown = 0.4f; // one shared cooldown, not per-pair — good enough to stop spam
        }

        // ── camera: only the active one is driven each frame — the other
        // just sits wherever it last was, ready to resume the moment TAB
        // switches back to it. The ship keeps flying under its own inputs
        // either way; this only decides which viewpoint is rendered. +
        if (!freeCam)
        {
            pickMarker->visible = false;
            previewGate->visible = false;

            Vec3 fwd = jet.forward();
            Vec3 desiredPos = shipPos - fwd * CamTuning::kDistance + Vec3(0.f, CamTuning::kHeight, 0.f);
            Vec3 desiredLook = shipPos + Vec3(0.f, CamTuning::kLookHeight, 0.f);
            camPosSpring.update(desiredPos, dt, CamTuning::kPosFrequency, CamTuning::kDamping);
            camLookSpring.update(desiredLook, dt, CamTuning::kLookFrequency, CamTuning::kDamping);
            chaseCam->set_position(camPosSpring.value);
            chaseCam->look_at(camLookSpring.value);

            // sense-of-speed FOV kick: widen the lens as speed climbs
            // (capped), eased so it doesn't snap — the same trick most
            // racing games use, since a fixed FOV makes fast motion read
            // as sluggish no matter how high the actual speed number is
            float t = jet.speed(dt) / 150.f; // matches HoverJet::maxSpeed
            if (t > 1.f) t = 1.f;
            float targetFov = 65.f + t * 20.f;
            camFov += (targetFov - camFov) * (1.f - expf(-6.f * dt));
            chaseCam->set_fov(camFov);
        }
        else
        {
            fly.apply(worldCam, dt);

            // + track editor: aim with the mouse, preview with a sphere +
            // ghost gate, LEFT/RIGHT rotates the pending facing +
            const Uint8* keys = SDL_GetKeyboardState(nullptr);
            if (keys[SDL_SCANCODE_LEFT]) pendingYaw += 2.0f * dt;
            else if (keys[SDL_SCANCODE_RIGHT]) pendingYaw -= 2.0f * dt;

            int mx, my;
            SDL_GetMouseState(&mx, &my);
            // window pixels -> pixels within the letterboxed/cropped content
            // rect (see the `vp` comment above) — screen_to_ray assumes its
            // pixel coords are already relative to the viewport it's given
            float localX = (float)(mx - vp.x), localY = (float)(my - vp.y);
            Vec3 rayDir = worldCam->screen_to_ray(localX, localY, vp.w, vp.h);
            Ray pickRay(worldCam->get_global_position(), rayDir);
            TerrainRaycastResult hit = terrain->raycast(pickRay, 3000.f);
            haveCursorHit = hit.hit;
            if (hit.hit)
            {
                cursorHit = hit.position + hit.normal * kLoopClearance;
                pickMarker->visible = true;
                pickMarker->set_position(hit.position);
                previewGate->visible = true;
                previewGate->set_position(cursorHit);
                previewGate->set_rotation(Quaternion::FromAxisAngle(Vec3(0.f, 1.f, 0.f), pendingYaw));
            }
            else
            {
                pickMarker->visible = false;
                previewGate->visible = false;
            }
        }

        if (audioReady) audioEngine.update();
        scene.update(dt);

        // camera aspect comes from kVirtualW/H, not the window size — see
        // SceneRenderer::render_fit(); that's what keeps the composition
        // identical across window sizes/modes, only the letterbox/crop
        // framing around it changes. Same `vp` rect the picking code above
        // already computed this frame, recomputed internally by
        // render_fit() too (cheap, keeps the two call sites independent).
        const int w = kVirtualW, h = kVirtualH;
        renderer.render_fit(scene, windowW, windowH, kVirtualW, kVirtualH, viewportMode);

        // + HUD: speed + altitude, drawn straight after the 3D pass +
        {
            float speed = jet.speed(dt);
            float alt = shipPos.y - terrain->height_at(shipPos.x, shipPos.z);
            char line1[64], line2[64], line3[64];
            snprintf(line1, sizeof(line1), "speed  %5.1f", speed);
            snprintf(line2, sizeof(line2), "alt    %5.1f", alt);
            if (kGateCount > 0)
                snprintf(line3, sizeof(line3), "lap %d | gate %d/%d", laps, nextGate + 1, kGateCount);
            else
                snprintf(line3, sizeof(line3), "no track — TAB, N, F5 to build one");

            Mat4 ortho = Mat4::Ortho(0.f, (float)w, (float)h, 0.f, -1.f, 1.f);
            hud.SetProjection(ortho.x);
            gl::Renderer::SetDepthTest(false);
            gl::Renderer::SetBlend(true);
            gl::Renderer::SetBlendFactors(gl::BlendFactor::SRC_ALPHA,
                                          gl::BlendFactor::ONE_MINUS_SRC_ALPHA);
            hud.SetColor(10, 14, 20, 190);
            hud.Rect(10.f, 10.f, 280.f, 66.f);
            hud.SetColor(150, 230, 255, 255);
            hud.Text(20.f, 18.f, 16.f, line1);
            hud.Text(20.f, 38.f, 16.f, line2);
            hud.SetColor(255, 220, 120, 255);
            hud.Text(20.f, 58.f, 16.f, line3);

            // ── 3, 2, 1, GO! — big and centered while gameplay is frozen ──
            if (raceCountdown > 0.f)
            {
                const char* text = raceCountdown > 3.f ? "3"
                                  : raceCountdown > 2.f ? "2"
                                  : raceCountdown > 1.f ? "1"
                                                        : "GO!";
                float size = 72.f;
                float tw = hud.TextWidth(size, text);
                hud.SetColor(255, 230, 90, 255);
                hud.Text((float)w * 0.5f - tw * 0.5f, (float)h * 0.35f, size, text);
            }

            // ── minimap: top-down track layout, top-right corner. World
            // (x,z) in [0,terrainSize] maps linearly onto the panel; z
            // flipped so "forward" on the actual map (+Z) reads as "up" on
            // the minimap, matching how the eye expects a top-down view. ──
            {
                const float kMapSize = 160.f, kMapMargin = 10.f;
                const float mapX0 = (float)w - kMapSize - kMapMargin, mapY0 = kMapMargin;
                auto toMap = [&](const Vec3& p) {
                    float u = p.x / terrainSize, v = 1.f - p.z / terrainSize;
                    return Vec2(mapX0 + u * kMapSize, mapY0 + v * kMapSize);
                };

                // background: the actual terrain texture, top-down —
                // reuses the already-loaded ground diffuse map instead of
                // a plain rect, so the minimap looks like the real map
                if (groundMat->diffuse)
                {
                    hud.SetColor(255, 255, 255, 230);
                    hud.Quad(*groundMat->diffuse, mapX0, mapY0, kMapSize, kMapSize, true, false);
                }
                else
                {
                    hud.SetColor(10, 14, 20, 190);
                    hud.Rect(mapX0, mapY0, kMapSize, kMapSize);
                }

                for (int i = 0; i < kGateCount; ++i)
                {
                    Vec2 p = toMap(gatePos[(size_t)i]);
                    bool isNext = i == nextGate;
                    if (isNext) hud.SetColor(255, 220, 120, 255);
                    else hud.SetColor(60, 60, 60, 220);
                    hud.Circle(p.x, p.y, isNext ? 3.5f : 2.f, true, 8);
                }
                // NPCs: red
                for (Npc& npc : npcs)
                {
                    Vec2 p = toMap(npc.jet.position());
                    hud.SetColor(230, 40, 40, 255);
                    hud.Circle(p.x, p.y, 2.5f, true, 8);
                }
                // player: white, + a short heading tick
                {
                    Vec2 p = toMap(shipPos);
                    Vec3 f = jet.forward();
                    Vec2 tip(p.x + f.x * 6.f, p.y - f.z * 6.f);
                    hud.SetColor(255, 255, 255, 255);
                    hud.Circle(p.x, p.y, 3.f, true, 10);
                    hud.ThickLine(p.x, p.y, tip.x, tip.y, 1.5f);
                }
            }

            hud.Render();
            gl::Renderer::SetBlend(false);
            gl::Renderer::SetDepthTest(true);
        }

        app.EndFrame();

        ++frame;
        if (maxFrames > 0 && frame >= maxFrames) running = false;
    }

    if (getenv("COREGL_GIF_AUTOSTART")) app.gif.Toggle(0, 0);
    printf("frames: %d\n", frame);
    if (audioReady) audioEngine.shutdown();
    hud.Release();
    scene.release_gpu();
    assets.release();
    renderer.release();
    app.Destroy();
    return 0;
}
