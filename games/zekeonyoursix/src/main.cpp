// Zeke On Your Six! — arcade flight sim
//
// Ported from tmp/apocalyx/demos/ZekeOnYourSix.lua (a Zero fighter over
// procedural terrain). The original solved a full 6DOF aerodynamic model
// (lift/drag/side-force curves, 1ms substeps) by hand in Lua; this port
// keeps the shape of the game — bank to turn, stall if you slow down,
// throttle for altitude, chase cam, minimap, smoke trail, crash-on-ground-
// contact — with an arcade flight model instead (see FlightPhysics.hpp).
//
// Controls: UP/DOWN pitch, LEFT/RIGHT bank+turn, Q/E rudder, PAGEUP/
// PAGEDOWN throttle, TAB free-fly camera (hold left mouse to look,
// WASD/QE to move), R reset, S toggle smoke, F10 gif, ESC quit.

#include "demo_app.hpp"
#include "demo_fly.hpp"
#include "FlightPhysics.hpp"
#include "audio.hpp"
#include <scene/ViewportFit.hpp>
#include <coregl/gl_batch.hpp>
#include <scene/AssetManager.hpp>
#include <scene/ByteArray.hpp>
#include <scene/Camera3D.hpp>
#include <scene/Filesystem.hpp>
#include <scene/Material.hpp>
#include <scene/Mesh.hpp>
#include <scene/MeshInstance.hpp>
#include <scene/ParticleSystemNode.hpp>
#include <scene/RibbonTrailNode.hpp>
#include <scene/Scene.hpp>
#include <scene/SceneRenderer.hpp>
#include <scene/TerrainNode.hpp>
#include <scene/LensFlareNode.hpp>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace CamTuning
{
constexpr float kDistance = 4.f;
constexpr float kHeight = 3.5f;
constexpr float kLookHeight = 1.f;
constexpr float kPosFrequency = 3.f;
constexpr float kLookFrequency = 5.5f;
constexpr float kDamping = 1.f;
} // namespace CamTuning

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
    if (!app.Create("coregl - zeke on your six")) return 1;
    printf("controls: UP/DOWN pitch | LEFT/RIGHT bank | Q/E rudder | PAGEUP/PAGEDOWN throttle | "
          "TAB free cam | R reset | S smoke | F10 gif | ESC\n");

    SceneRenderer renderer;
    if (!renderer.init() || !renderer.enable_shadows(4, 2048, 600.f))
    {
        fprintf(stderr, "renderer init failed\n");
        app.Destroy();
        return 1;
    }
    // sun sits low, slightly behind-right (0, 0.342, 0.9397 in the Lua demo
    // is the direction TOWARD the sun; set_light_dir wants the direction
    // the light travels, i.e. the opposite)
    renderer.set_light_dir(Vec3(-0.15f, -0.55f, -0.82f));
    renderer.enable_post();
    renderer.set_bloom_params(1.8f, 0.6f);

    Scene scene;
    assets::AssetManager& assets = assets::AssetManager::instance();

    // ── sky: the Lua demo's "half sky" faces are non-square panels (512x256
    // side strips + a 128x128 top cap), not a real 6-face cube — the engine's
    // loadCubemap requires equal square faces, so rather than stretch/crop
    // those into something that doesn't match anymore, use the renderer's
    // procedural gradient sky instead; it already keys off the light
    // direction set above, giving the same low sunset-orange horizon look.
    renderer.set_sky_enabled(true);

    // ── terrain: same heightmap as the original, rescaled to a size that
    // suits our arcade top speed (~220 units/s) instead of the Lua demo's
    // literal feet-based 512*30 x 32*30 scale ──
    TerrainNode* terrain = scene.root().create_child<TerrainNode>("terrain");
    const float terrainSize = 6000.f, terrainHeight = 380.f;
    if (!terrain->load_heightmap("games/zekeonyoursix/assets/terrain.png", terrainSize, terrainHeight,
                                 terrainSize, 40.f, 40.f))
    {
        fprintf(stderr, "terrain heightmap load failed (run from repo root)\n");
        app.Destroy();
        return 1;
    }
    Material* groundMat = scene.create_material();
    groundMat->diffuse = assets.loadTexture("zeke_coarse", "games/zekeonyoursix/assets/coarse.jpg");
    groundMat->detail = assets.loadTexture("zeke_detail", "games/zekeonyoursix/assets/detail.jpg");
    groundMat->detail_scale = 90.f;
    terrain->set_material(groundMat);

    // ── the Zero fighter ──
    std::vector<Material*> shipMats;
    Mesh* shipMesh = assets.load_3ds_mesh("zero", "games/zekeonyoursix/assets/zero.3ds", shipMats);
    if (!shipMesh)
    {
        fprintf(stderr, "zero.3ds load failed (run from repo root)\n");
        app.Destroy();
        return 1;
    }
    for (Material* m : shipMats)
    {
        m->specular = 0.4f;
        m->shininess = 64.f;
    }
    MeshInstance* ship = scene.root().create_child<MeshInstance>("zero");
    ship->set_mesh(shipMesh);
    ship->set_materials(shipMats);
    // scale so the model's own bounds come out to roughly an 8-unit
    // wingspan regardless of the .3ds file's native units
    Vec3 shipBounds = shipMesh->bounds().max - shipMesh->bounds().min;
    float widestAxis = shipBounds.x > shipBounds.z ? shipBounds.x : shipBounds.z;
    float shipScale = widestAxis > 0.001f ? 8.f / widestAxis : 1.f;
    ship->set_scale(shipScale);

    const Quaternion meshForwardFix = Quaternion::FromAxisAngle(Vec3(0.f, 1.f, 0.f), 3.14159265f);
    printf("zero.3ds bounds: %.2f x %.2f x %.2f, scale %.4f\n", shipBounds.x, shipBounds.y, shipBounds.z,
          shipScale);

    // ── engine exhaust smoke trail — one emitter at the tail, continuous
    // rate tied to throttle (see the main loop) ──
    ParticleSystemNode* smoke = ship->create_child<ParticleSystemNode>("smoke", 400);
    smoke->texture = assets.loadTexture("zeke_smoke", "games/zekeonyoursix/assets/smoke.png");
    smoke->blend = ParticleBlendMode::Additive;
    smoke->set_position(0.f, shipMesh->bounds().min.y * 0.2f, shipMesh->bounds().min.z * 1.1f);
    smoke->setContinuous(0.f)
        ->setShapeCircle(0.15f)
        ->setEmissionDirection(Vec3(0.f, 0.f, -1.f)) // zero.3ds nose is local +Z, so tail trails toward -Z
        ->setSpreadAngle(8.f)
        ->setSpeed(0.5f, 1.5f)
        ->setLifetime(1.2f, 2.f)
        ->setSize(Vec2(0.6f, 0.6f), Vec2(3.f, 3.f))
        ->setColor(Vec4(0.6f, 0.6f, 0.6f, 0.5f), Vec4(0.8f, 0.8f, 0.8f, 0.f));
    bool smokeOn = true;

    // ── flight model: spawn high above the terrain's center, flying
    // level toward -Z ──
    FlightPhysics plane;
    Vec3 spawnPos(terrainSize * 0.5f, 0.f, terrainSize * 0.5f);
    spawnPos.y = terrain->height_at(spawnPos.x, spawnPos.z) + 120.f;
    plane.init(spawnPos, Quaternion(), 60.f);

    // ── wingtip vapor ribbons: like hoverjet's wing_trails, but spanwise
    // (root to mid-wing) instead of a short chordwise blade at the tip —
    // each wing is a base/mid pair of plain Node3D children of the ship
    // (so they follow its transform automatically, no per-frame position
    // update needed), lit up only while the plane is actually banking hard
    // (see the turn-rate check in the main loop below) ──
    float wingRootX = shipMesh->bounds().max.x * 0.15f;  // just outside the fuselage
    float wingMidX = shipMesh->bounds().max.x * 0.55f;   // partway to the tip
    float wingY = 0.f, wingZ = (shipMesh->bounds().min.z + shipMesh->bounds().max.z) * 0.5f;
    Node3D* wingLRoot = ship->create_child<Node3D>("wing_l_root");
    wingLRoot->set_position(-wingRootX, wingY, wingZ);
    Node3D* wingLMid = ship->create_child<Node3D>("wing_l_mid");
    wingLMid->set_position(-wingMidX, wingY, wingZ);
    Node3D* wingRRoot = ship->create_child<Node3D>("wing_r_root");
    wingRRoot->set_position(wingRootX, wingY, wingZ);
    Node3D* wingRMid = ship->create_child<Node3D>("wing_r_mid");
    wingRMid->set_position(wingMidX, wingY, wingZ);

    RibbonTrailNode* wingTrails = scene.root().create_child<RibbonTrailNode>("wing_trails", 2, 40);
    wingTrails->setTrailLength(1.4f);
    wingTrails->blend = ParticleBlendMode::Additive;
    wingTrails->depthTest = true;
    wingTrails->addBladeChain(wingLRoot, wingLMid, Vec4(0.55f, 0.8f, 1.f, 0.7f), Vec4(1.f, 1.f, 1.f, 0.f),
                              0.5f, 1.f, 0.f);
    wingTrails->addBladeChain(wingRRoot, wingRMid, Vec4(0.55f, 0.8f, 1.f, 0.7f), Vec4(1.f, 1.f, 1.f, 0.f),
                              0.5f, 1.f, 0.f);
    wingTrails->setEmitting(false); // only while turning hard — see the main loop
    const float kTrailTurnRateThreshold = 0.6f; // rad/s of actual heading change before it lights up
    Vec3 trailPrevForward = plane.forward();

    // ── propeller: a pivot node at the nose, spinning about the nose-tail
    // axis (local +Z — see the meshForwardFix comment above) at a rate
    // driven by throttle in the main loop; the 2 crossed blades are its
    // children at the local origin, so only the pivot's rotation needs
    // touching per frame instead of recomposing both blades' quaternions ──
    Mesh* propBladeMesh = assets.createCube("prop_blade", 1.6f, 0.14f, 0.05f);
    Material* propMat = scene.create_material(0.05f, 0.05f, 0.05f);
    propMat->unlit = true;
    Node3D* propPivot = ship->create_child<Node3D>("prop_pivot");
    propPivot->set_position(0.f, 0.f, shipMesh->bounds().max.z + 0.35f); // just ahead of the nose tip
    MeshInstance* propBladeA = propPivot->create_child<MeshInstance>("prop_blade_a");
    propBladeA->set_mesh(propBladeMesh);
    propBladeA->set_material(propMat);
    MeshInstance* propBladeB = propPivot->create_child<MeshInstance>("prop_blade_b");
    propBladeB->set_mesh(propBladeMesh);
    propBladeB->set_material(propMat);
    propBladeB->set_rotation(Quaternion::FromAxisAngle(Vec3(0.f, 0.f, 1.f), 1.57079633f)); // set once
    float propAngle = 0.f; // accumulated per frame in the main loop

    LensFlareNode* flare = scene.root().create_child<LensFlareNode>("sun_flare");
    flare->init_default_flares();

    // ── audio: engine hum (looping SFX, pitched by throttle+speed) ──
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
    audio::Engine::SoundId engineSnd = 0;
    audio::Engine::Handle engineVoice = 0;
    if (audioReady)
    {
        engineSnd = loadSound(audioEngine, "games/zekeonyoursix/assets/motor.wav", false);
        if (engineSnd) engineVoice = audioEngine.playSfxLoop(engineSnd, 0.f);
    }
    else
        fprintf(stderr, "audio: init failed, running without sound\n");

    // two independent cameras: chase (game) and free-fly (TAB)
    Camera3D* chaseCam = scene.root().create_child<Camera3D>("chase");
    chaseCam->set_perspective(65.f, 0.5f, 8000.f);
    Camera3D* worldCam = scene.root().create_child<Camera3D>("world");
    worldCam->set_perspective(70.f, 0.5f, 8000.f);

    bool freeCam = false;
    FlyCam fly;
    fly.speed = 300.f; // terrain is 6000 units wide

    const int kVirtualW = 1280, kVirtualH = 720;
    ViewportFitMode viewportMode = ViewportFitMode::Letterbox;
    bool bloomOn = true;
    float camFov = 65.f; // eased target for the chase cam's speed-based FOV kick below

    DampedSpring3 camPosSpring, camLookSpring;
    auto seedCamera = [&]() {
        Vec3 shipPos = plane.position;
        Vec3 fwd = plane.forward();
        Vec3 pos = shipPos - fwd * CamTuning::kDistance + Vec3(0.f, CamTuning::kHeight, 0.f);
        camPosSpring.seed(pos);
        camLookSpring.seed(shipPos + Vec3(0.f, CamTuning::kLookHeight, 0.f));
        worldCam->set_position(pos);
        worldCam->look_at(shipPos);
        Vec3 e = worldCam->get_euler();
        fly.pitch = e.x;
        fly.yaw = e.y;
    };
    seedCamera();

    auto resetGame = [&]() {
        plane.init(spawnPos, Quaternion(), 60.f);
        seedCamera();
        printf("reset\n");
    };

    scene.set_active_camera(chaseCam);
    scene.ready();

    gl::Batch hud;
    hud.Init();

    gl::u64 lastTicks = SDL_GetPerformanceCounter();
    const gl::u64 freq = SDL_GetPerformanceFrequency();

    int frame = 0;
    bool running = true;
    bool crashed = false;
    float crashTimer = 0.f;
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
                }
                if (down && sc == SDL_SCANCODE_F7)
                {
                    bloomOn = !bloomOn;
                    renderer.set_bloom_enabled(bloomOn);
                }
                if (down && sc == SDL_SCANCODE_R) resetGame(), crashed = false;
                if (down && sc == SDL_SCANCODE_S) smokeOn = !smokeOn;
                if (down && sc == SDL_SCANCODE_TAB)
                {
                    freeCam = !freeCam;
                    scene.set_active_camera(freeCam ? worldCam : chaseCam);
                }
            }
            fly.handle(ev);
        }
        if (!running) break;

        gl::u64 now = SDL_GetPerformanceCounter();
        float dt = (float)((double)(now - lastTicks) / (double)freq);
        lastTicks = now;
        if (dt > 0.1f) dt = 0.1f;

        int windowW, windowH;
        app.DrawableSize(&windowW, &windowH);

        if (!crashed)
        {
            const Uint8* keys = SDL_GetKeyboardState(nullptr);
            plane.pitchInput = 0.f;
            if (keys[SDL_SCANCODE_UP]) plane.pitchInput -= 1.f;
            if (keys[SDL_SCANCODE_DOWN]) plane.pitchInput += 1.f;
            plane.rollInput = 0.f;
            if (keys[SDL_SCANCODE_RIGHT]) plane.rollInput += 1.f;
            if (keys[SDL_SCANCODE_LEFT]) plane.rollInput -= 1.f;
            plane.yawInput = 0.f;
            if (keys[SDL_SCANCODE_E]) plane.yawInput += 1.f;
            if (keys[SDL_SCANCODE_Q]) plane.yawInput -= 1.f;
            if (keys[SDL_SCANCODE_PAGEUP]) plane.throttle += dt * 0.5f;
            if (keys[SDL_SCANCODE_PAGEDOWN]) plane.throttle -= dt * 0.5f;
            if (plane.throttle < 0.f) plane.throttle = 0.f;
            if (plane.throttle > 1.f) plane.throttle = 1.f;

            plane.update(dt);
        }

        Vec3 shipPos = plane.position;
        ship->set_position(shipPos);
        ship->set_rotation(plane.orientation * meshForwardFix);

        {
            // idle windmilling even at 0 throttle, spinning up fast toward
            // full RPM as throttle climbs — the blades' fixed 90° offset is
            // baked into propBladeB's local rotation, set once above, so
            // spinning the whole prop is just one rotation on the pivot
            float propSpinRate = 3.f + plane.throttle * 55.f; // rad/s
            propAngle += propSpinRate * dt;
            propPivot->set_rotation(Quaternion::FromAxisAngle(Vec3(0.f, 0.f, 1.f), propAngle));
        }

        {
            float rate = (!crashed && smokeOn) ? (30.f + plane.throttle * 90.f) : 0.f;
            smoke->setContinuous(rate);
        }

        // ── wing vapor ribbons: only while the nose is actually swinging
        // fast (hard bank), not just while LEFT/RIGHT is held ──
        {
            Vec3 curForward = plane.forward();
            float cosAngle = Vec3::Dot(trailPrevForward, curForward);
            cosAngle = cosAngle < -1.f ? -1.f : (cosAngle > 1.f ? 1.f : cosAngle);
            float turnRate = dt > 0.f ? acosf(cosAngle) / dt : 0.f;
            wingTrails->setEmitting(!crashed && turnRate > kTrailTurnRateThreshold);
            trailPrevForward = curForward;
        }

        if (audioReady && engineVoice)
        {
            float speedFrac = plane.speed() / plane.maxSpeed;
            if (speedFrac > 1.f) speedFrac = 1.f;
            audioEngine.setVolume(engineVoice, crashed ? 0.f : (0.25f + plane.throttle * 0.65f));
            audioEngine.setPitch(engineVoice, 0.7f + speedFrac * 0.8f);
        }

        // ── crash: nose (or any part) touching the ground ──
        float groundH = terrain->height_at(shipPos.x, shipPos.z);
        if (!crashed && shipPos.y < groundH + 1.5f)
        {
            crashed = true;
            crashTimer = 0.f;
            printf("crashed! altitude %.1f vs ground %.1f\n", shipPos.y, groundH);
        }
        if (crashed)
        {
            crashTimer += dt;
            if (crashTimer > 2.f) resetGame(), crashed = false;
        }

        if (!freeCam)
        {
            Vec3 fwd = plane.forward();
            Vec3 desiredPos = shipPos - fwd * CamTuning::kDistance + Vec3(0.f, CamTuning::kHeight, 0.f);
            Vec3 desiredLook = shipPos + Vec3(0.f, CamTuning::kLookHeight, 0.f);
            camPosSpring.update(desiredPos, dt, CamTuning::kPosFrequency, CamTuning::kDamping);
            camLookSpring.update(desiredLook, dt, CamTuning::kLookFrequency, CamTuning::kDamping);
            float camGround = terrain->height_at(camPosSpring.value.x, camPosSpring.value.z);
            if (camPosSpring.value.y < camGround + 2.f) camPosSpring.value.y = camGround + 2.f;
            chaseCam->set_position(camPosSpring.value);
            // world-up, not plane.up(): a chase cam that fully banks with
            // the plane spins the whole screen on every roll input, which
            // reads as LEFT/RIGHT doing something broken/sideways instead
            // of a normal turn — keeping the camera upright while it still
            // tracks the plane's position/heading is what every arcade
            // flight/chase cam actually does
            chaseCam->look_at(camLookSpring.value);

            // sense-of-speed FOV kick: widen the lens as speed climbs
            // (capped), eased so it doesn't snap — same trick hoverjet uses
            float t = plane.speed() / plane.maxSpeed;
            if (t > 1.f) t = 1.f;
            float targetFov = 65.f + t * 22.f;
            camFov += (targetFov - camFov) * (1.f - expf(-6.f * dt));
            chaseCam->set_fov(camFov);
        }
        else
        {
            fly.apply(worldCam, dt);
        }

        if (audioReady) audioEngine.update();
        scene.update(dt);

        renderer.render_fit(scene, windowW, windowH, kVirtualW, kVirtualH, viewportMode);

        {
            const int w = kVirtualW, h = kVirtualH;
            float speed = plane.speed();
            float alt = shipPos.y - groundH;
            char line1[64], line2[64], line3[64];
            snprintf(line1, sizeof(line1), "speed  %5.1f", speed);
            snprintf(line2, sizeof(line2), "alt    %5.1f", alt);
            snprintf(line3, sizeof(line3), "thrust %3.0f%%", plane.throttle * 100.f);

            Mat4 ortho = Mat4::Ortho(0.f, (float)w, (float)h, 0.f, -1.f, 1.f);
            hud.SetProjection(ortho.x);
            gl::Renderer::SetDepthTest(false);
            gl::Renderer::SetBlend(true);
            gl::Renderer::SetBlendFactors(gl::BlendFactor::SRC_ALPHA,
                                          gl::BlendFactor::ONE_MINUS_SRC_ALPHA);
            hud.SetColor(10, 14, 20, 190);
            hud.Rect(10.f, 10.f, 200.f, 66.f);
            hud.SetColor(150, 230, 255, 255);
            hud.Text(20.f, 18.f, 16.f, line1);
            hud.Text(20.f, 38.f, 16.f, line2);
            hud.SetColor(255, 220, 120, 255);
            hud.Text(20.f, 58.f, 16.f, line3);

            if (crashed)
            {
                const char* text = "CRASHED — press R";
                float size = 40.f;
                float tw = hud.TextWidth(size, text);
                hud.SetColor(255, 80, 60, 255);
                hud.Text((float)w * 0.5f - tw * 0.5f, (float)h * 0.4f, size, text);
            }

            // minimap: top-down terrain, top-right corner
            {
                const float kMapSize = 160.f, kMapMargin = 10.f;
                const float mapX0 = (float)w - kMapSize - kMapMargin, mapY0 = kMapMargin;
                auto toMap = [&](const Vec3& p) {
                    float u = p.x / terrainSize, v = 1.f - p.z / terrainSize;
                    return Vec2(mapX0 + u * kMapSize, mapY0 + v * kMapSize);
                };
                if (groundMat->diffuse)
                {
                    hud.SetColor(255, 255, 255, 230);
                    hud.Quad(*groundMat->diffuse, mapX0, mapY0, kMapSize, kMapSize, true, false);
                }
                Vec2 p = toMap(shipPos);
                Vec3 f = plane.forward();
                Vec2 tip(p.x + f.x * 8.f, p.y - f.z * 8.f);
                hud.SetColor(255, 255, 255, 255);
                hud.Circle(p.x, p.y, 3.f, true, 10);
                hud.ThickLine(p.x, p.y, tip.x, tip.y, 1.5f);
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
