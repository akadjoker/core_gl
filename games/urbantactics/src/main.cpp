// Urban Tactics — test scene: a real Quake3 city level converted offline
// (tools/bspx_to_h3d.py, from tmp/apocalyx/demos/UrbanTactics.lua's
// city.bsx — an Apocalyx-engine-specific flattened BSP dump, not idTech3
// IBSP) into a static coregl .h3d mesh, walked around by a genuine Q3
// player model (spacemarine40k: lower/upper/head .MD3, tag-attached,
// animated via the engine's native MD3/MorphMeshInstance pipeline — no
// new engine code for the character at all).
//
// Controls: WASD move (always the player, even in free cam), SHIFT run,
//           mouse aim (small clamped cone — fixed third-person over-the-
//           shoulder, not a free-look camera), left click fire (works in
//           both camera modes), TAB free-fly debug camera (arrows to
//           move, hold RIGHT mouse to look, Q/E up/down — plain Node3D/
//           Camera3D calls, not the demos/ FlyCam helper: this game is
//           meant to stand on its own, engine nodes only, same as it'd
//           have to in a real shipped game), F10 gif, ESC quit.

#include "demo_app.hpp"
#include "QuakeAssets.hpp"
#include <coregl/gl_batch.hpp>
#include <scene/AssetManager.hpp>
#include <scene/BillboardNode.hpp>
#include <scene/Camera3D.hpp>
#include <scene/Collision.hpp>
#include <scene/DecalSystemNode.hpp>
#include <scene/Material.hpp>
#include <scene/Mesh.hpp>
#include <scene/MeshInstance.hpp>
#include <scene/MorphMeshInstance.hpp>
#include <scene/ParticleSystemNode.hpp>
#include <scene/Scene.hpp>
#include <scene/SceneRenderer.hpp>
#include <scene/TagAttachment.hpp>
#include <scene/ViewportFit.hpp>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

static const char* kAssetDir = "games/urbantactics/assets/";
static const char* kMarineDir = "games/urbantactics/assets/marine/";
static const char* kGunDir = "games/urbantactics/assets/machinegun/";

// Free-fly debug camera: mouse-drag look + WASD/QE move, built directly on
// Camera3D/Node3D (set_euler/advance/strafe/move_global) rather than
// demos/demo_fly.hpp's FlyCam — this game is meant to exercise the engine
// the way a real shipped game's own code would, not lean on demo
// scaffolding for something as basic as a debug camera.
struct DebugFlyCam
{
    float yaw = 0.f, pitch = -0.15f;
    float speed = 25.f;
    bool looking = false;

    void handle(const SDL_Event& ev)
    {
        // right-click, not left — left-click fires the weapon in BOTH
        // camera modes now, so it can't also be the free-cam look-hold
        if (ev.type == SDL_MOUSEBUTTONDOWN && ev.button.button == SDL_BUTTON_RIGHT) looking = true;
        if (ev.type == SDL_MOUSEBUTTONUP && ev.button.button == SDL_BUTTON_RIGHT) looking = false;
        if (ev.type == SDL_MOUSEMOTION && looking)
        {
            yaw -= (float)ev.motion.xrel * 0.005f;
            pitch -= (float)ev.motion.yrel * 0.005f;
            if (pitch > 1.55f) pitch = 1.55f;
            if (pitch < -1.55f) pitch = -1.55f;
        }
    }

    void apply(Camera3D* cam, float dt)
    {
        cam->set_euler(Vec3(pitch, yaw, 0.f));
        const Uint8* keys = SDL_GetKeyboardState(nullptr);
        float v = keys[SDL_SCANCODE_LSHIFT] ? speed * 4.f : speed;
        // arrow keys, not WASD — WASD stays the player's the whole time,
        // free-cam or not, so you can watch the player do its thing from
        // outside instead of it freezing whenever you look away from it
        if (keys[SDL_SCANCODE_UP]) cam->advance(v * dt);
        if (keys[SDL_SCANCODE_DOWN]) cam->advance(-v * dt);
        if (keys[SDL_SCANCODE_LEFT]) cam->strafe(-v * dt);
        if (keys[SDL_SCANCODE_RIGHT]) cam->strafe(v * dt);
        if (keys[SDL_SCANCODE_Q]) cam->move_global(Vec3(0.f, -v * dt, 0.f));
        if (keys[SDL_SCANCODE_E]) cam->move_global(Vec3(0.f, v * dt, 0.f));
    }
};


static MorphMeshInstance* load_part(Node* parent, const char* name, const char* md3File,
                                    const char* skinFile)
{
    assets::AssetManager& assets = assets::AssetManager::instance();
    MorphKeyframes keyframes;
    std::vector<Material*> mats;
    MorphTags tags;
    std::string path = std::string(kMarineDir) + md3File;
    Mesh* loaded = assets.load_md3_mesh(name, path.c_str(), keyframes, mats, &tags, kMarineDir);
    if (!loaded)
    {
        fprintf(stderr, "failed to load %s\n", path.c_str());
        return nullptr;
    }
    apply_skin_textures(mats, std::string(kMarineDir) + skinFile, kMarineDir, name);

    MorphMeshInstance* inst = parent->create_child<MorphMeshInstance>(name);
    inst->set_anim_data(loaded, std::move(keyframes), std::move(tags));
    inst->set_materials(mats);
    return inst;
}

 
static void emit_bullet_impact(DecalSystemNode* decals, ParticleSystemNode* sparks, const Vec3& pos,
                               const Vec3& normal)
{
    decals->add(pos, normal, Vec2(0.12f, 0.12f), Vec4(1.f, 1.f, 1.f, 1.f), 25.f);
    sparks->set_position(pos + normal * 0.05f);
    sparks->setEmissionDirection(normal);
    sparks->emitBurst(14);
}

int main(int argc, char** argv)
{
    const int maxFrames = (argc > 1) ? atoi(argv[1]) : 0;

    DemoApp app;
    if (!app.Create("coregl - urban tactics")) return 1;
    printf("controls: WASD move (always) | SHIFT run | mouse aim (clamped) | left click fire "
          "(either cam mode) | TAB free cam (arrows move, RMB look) | L/P page level surface "
          "debug box | F10 gif | ESC\n");

 
    fs::getFilesystem().addFolder("../../../..");
    fs::getFilesystem().addFolder("../../../../..");

    SceneRenderer renderer;
 
    if (!renderer.init() || !renderer.enable_shadows(4, 2024, 200.f))
    {
        fprintf(stderr, "renderer init failed\n");
        app.Destroy();
        return 1;
    }
    renderer.set_light_dir(Vec3(-0.35f, -0.75f, -0.45f));
    // default (0.05) is tuned for vehicle/building-scale demos (tens of
    // world units) — this scene's character is ~1 world unit tall, so
    // that push reads as a peter-panning gap at floor/wall contacts. This
    // call went missing during earlier edits and the gap came right back;
    // no amount of moving the floor geometry fixes a shading-side bug.
    renderer.set_shadow_normal_bias(0.01f);
    renderer.set_shadows_active(true);
    renderer.set_sky_enabled(true);
//    renderer.enable_post();
//    renderer.set_bloom_params(1.5f, 0.6f);

    Scene scene;
    assets::AssetManager& assets = assets::AssetManager::instance();
 
    // building it's standing next to stay proportional to each other
    const float kModelScale = 0.045f;

 
    // hand-edited in Blender (see tools/bspx_to_obj.py) — the spike
    // triangle and the shadow peter-panning gap were fixed by hand on the
    // geometry itself, not chased further through shader/shadow-pass bias
    std::vector<Material*> levelMats;
    Mesh* levelMesh = assets.load_obj_mesh(
        "city", (std::string(kAssetDir) + "city_edit.obj").c_str(), levelMats, kAssetDir);
    if (!levelMesh)
    {
        fprintf(stderr, "city.obj load failed \n");
        app.Destroy();
        return 1;
    }
    MeshInstance* level = scene.root().create_child<MeshInstance>("city");
    level->set_mesh(levelMesh);
    level->set_materials(levelMats);
    //level->set_scale(kModelScale);
    levelMesh->dump_surfaces("city"); // console: index/material_slot/bounds per surface
    // floor (surface 1, confirmed via L/P debug box) nudged up a hair —
    // closes the shadow peter-panning gap at wall/floor contacts with real
    // geometry overlap instead of shader/shadow-pass bias tuning
    levelMesh->transform_surface(0, Mat4::Translate(Vec3(0.f, 0.02f, 0.f)));

    // ── world collision: a plain triangle-soup CollisionSystem built
    // straight from the level mesh's own CPU-resident vertex/index data
    // (scene/Collision.hpp — Moller-Trumbore rayCast), same technique
    // demos/bsp/main.cpp uses for its BSP. Scaled by kModelScale up front
    // so every query after this is in plain world-space units, matching
    // playerPos/camera — no per-query rescaling. ──
    CollisionSystem worldCollision;
    {
        const std::vector<MeshVertex>& verts = levelMesh->vertices();
        const std::vector<u32>& indices = levelMesh->indices();
        std::vector<Triangle> tris;
        tris.reserve(indices.size() / 3);
        for (size_t i = 0; i + 2 < indices.size(); i += 3)
            tris.emplace_back(verts[indices[i]].position * kModelScale,
                              verts[indices[i + 1]].position * kModelScale,
                              verts[indices[i + 2]].position * kModelScale);
        worldCollision.addTriangles(tris);
        printf("world collision: %d triangles\n", worldCollision.triangleCount());
    }

    MorphMeshInstance* lower = load_part(&scene.root(), "lower", "lower.MD3", "lower_default.skin");
    if (!lower)
    {
        app.Destroy();
        return 1;
    }

    // formula derived from LEGS_WALK/LEGS_RUN 
    // eye: real_first = cfg_first - 71, real_count = cfg_count + 1 (i.e.
    // real_last = cfg_last - 70)  
 
    lower->add_clip("LEGS_IDLE", 187, 197, 15.f, true);
    lower->add_clip("LEGS_WALK", 100, 113, 15.f, true);
    lower->add_clip("LEGS_RUN", 117, 128, 20.f, true);
    lower->add_clip("LEGS_BACK", 129, 140, 15.f, true);
    lower->add_clip("LEGS_JUMP", 152, 158, 15.f, false);
    lower->add_clip("LEGS_LAND", 160, 171, 15.f, false);
    lower->add_clip("LEGS_TURN", 210, 217, 15.f, true);

 
    lower->add_clip("BOTH_DEATH1", 0, 27, 30.f, false);
    lower->add_clip("BOTH_DEAD1", 28, 28, 30.f, false);
    lower->add_clip("BOTH_DEATH2", 30, 57, 30.f, false);
    lower->add_clip("BOTH_DEAD2", 58, 58, 30.f, false);
    lower->add_clip("BOTH_DEATH3", 60, 88, 30.f, false);
    lower->add_clip("BOTH_DEAD3", 89, 89, 30.f, false);

     TagAttachment* torsoAttach = lower->create_child<TagAttachment>("torso_attach");
    if (!torsoAttach->attach(lower, "tag_torso"))
        fprintf(stderr, "lower.MD3 has no tag_torso\n");
    MorphMeshInstance* upper = load_part(torsoAttach, "upper", "upper.MD3", "upper_default.skin");

    upper->add_clip("TORSO_STAND", 159, 159, 30.f, true);
    upper->add_clip("TORSO_STAND2", 160, 160, 30.f, true);
    upper->add_clip("TORSO_ATTACK", 161, 161, 30.f, true);
    upper->add_clip("TORSO_ATTACK2", 162, 162, 30.f, true);
    upper->add_clip("TORSO_DROP", 163, 163, 30.f, true);
    upper->add_clip("TORSO_RAISE", 164, 164, 30.f, true);
    upper->add_clip("TORSO_GESTURE", 165, 165, 30.f, true);
 

    TagAttachment* headAttach = nullptr;
    MorphMeshInstance* head = nullptr;
    if (upper)
    {
        headAttach = upper->create_child<TagAttachment>("head_attach");
        if (!headAttach->attach(upper, "tag_head"))
            fprintf(stderr, "upper.MD3 has no tag_head\n");
        head = load_part(headAttach, "head", "head.MD3", "head_default.skin");
    }
    if (!upper || !head)
    {
        app.Destroy();
        return 1;
    }
 
    TagAttachment* weaponAttach = nullptr;
    BillboardNode* muzzleFlash = nullptr; // toggled visible briefly on fire 
    if (upper)
    {
        weaponAttach = upper->create_child<TagAttachment>("weapon_attach");
        if (!weaponAttach->attach(upper, "tag_weapon"))
            fprintf(stderr, "upper.MD3 has no tag_weapon\n");

        MorphKeyframes gunAnim;
        std::vector<Material*> gunMats;
        MorphTags gunTags;
        std::string gunPath = std::string(kGunDir) + "machinegun.md3";
        Mesh* gunMesh = assets.load_md3_mesh("machinegun", gunPath.c_str(), gunAnim, gunMats, &gunTags, kGunDir);
        if (gunMesh)
        {
  
            gl::Texture* gunTex = assets.loadTexture("machinegun_diffuse", (std::string(kGunDir) + "machinegun.jpg").c_str());
            for (Material* m : gunMats)
                if (m) m->diffuse = gunTex;

            MorphMeshInstance* weapon = weaponAttach->create_child<MorphMeshInstance>("weapon");
            weapon->set_anim_data(gunMesh, std::move(gunAnim), std::move(gunTags));
            weapon->set_materials(gunMats);

            TagAttachment* flashAttach = weapon->create_child<TagAttachment>("flash_attach");
            if (!flashAttach->attach(weapon, "tag_flash"))
                fprintf(stderr, "machinegun.md3 has no tag_flash\n");

            muzzleFlash = flashAttach->create_child<BillboardNode>("flash");
            muzzleFlash->texture =
                assets.loadTexture("machinegun_flash_diffuse", (std::string(kAssetDir) + "gfx/f_machinegun.jpg").c_str());
            muzzleFlash->blend = ParticleBlendMode::Additive;
            //muzzleFlash->depthTest = false; // additive muzzle flash must glow over the gun/geometry
            muzzleFlash->set_view_mode(BillboardViewMode::Free);
            muzzleFlash->set_size(2.35f, 2.35f);
            muzzleFlash->set_color(Vec4(1.f, 0.9f, 0.6f, 0.f));
        }
        else
            fprintf(stderr, "failed to load %s\n", gunPath.c_str());
    }

    lower->play("LEGS_TURN");
    upper->play("TORSO_STAND");

 
    const float kGroundY = 8.f * kModelScale;
    float feetOffset = -lower->get_mesh()->bounds().min.y * kModelScale;
    Vec3 playerPos(0.f, kGroundY + feetOffset, 0.f);
    float playerYaw = 0.f;
    float camYaw = 0.f; // eased toward playerYaw each frame — see the main loop
    lower->set_scale(kModelScale);
    printf("lower.MD3 bounds.min.y = %.3f raw -> feetOffset = %.3f world\n",
          lower->get_mesh()->bounds().min.y, feetOffset);

    const Quaternion kMeshForwardFix = Quaternion::FromAxisAngle(Vec3(0.f, 1.f, 0.f), 1.57079633f);

 
    Camera3D* cam = scene.root().create_child<Camera3D>("cam");
    cam->set_perspective(60.f, 0.1f, 4000.f);
    float aimYaw = 0.f, aimPitch = 0.f;
    const float kAimYawLimit = 0.78539816f; // 45 degrees either side of playerYaw
    const float kAimPitchLimit = 0.35f; // ~20 degrees up/down

 
    Camera3D* worldCam = scene.root().create_child<Camera3D>("world");
    worldCam->set_perspective(70.f, 0.1f, 4000.f);
    worldCam->set_position(playerPos + Vec3(0.f, 2.f, 3.f));
    bool freeCam = false;
    int debugSurfaceIndex = -1; // L/P page through levelMesh's surfaces (see below)
    DebugFlyCam fly;
    fly.speed = 25.f; // city spans ~+-94 scaled units, walking speed is 3-6.5

 
    // bullet_mrk.PNG — a real RGBA asset with an actual alpha channel
    // (verified: alpha ranges 0-255, not flat), unlike hole_lg_mrk.jpg —
    // works correctly with Alpha blend, no procedural texture needed
    gl::Texture* decalTex = assets.loadTexture("bullet_decal", (std::string(kAssetDir) + "gfx/bullet_mrk.PNG").c_str());
    DecalSystemNode* decals = scene.root().create_child<DecalSystemNode>("decals", 128);
    decals->texture = decalTex;
    decals->blend = ParticleBlendMode::Alpha;

    ParticleSystemNode* sparks = scene.root().create_child<ParticleSystemNode>("sparks", 64);
    sparks->texture = assets.loadTexture("spark_glow", "assets/textures/light.jpg");
    sparks->blend = ParticleBlendMode::Additive;
 
    sparks->setContinuous(0.f)
        ->setShapeCone(35.f, 0.02f)
        ->setSpeed(1.f, 4.f)
        ->setLifetime(0.25f, 0.5f)
        ->setSize(Vec2(0.4f, 0.4f), Vec2(0.05f, 0.05f))
        ->setColor(Vec4(1.f, 0.9f, 0.5f, 1.f), Vec4(1.f, 0.3f, 0.05f, 0.f))
        ->setGravity(Vec3(0.f, -9.f, 0.f));

 
    struct Bullet
    {
        Vec3 pos{0.f, 0.f, 0.f}, vel{0.f, 0.f, 0.f};
        float ttl = 0.f;
        bool active = false;
        MeshInstance* visual = nullptr;
    };
    const int kMaxBullets = 32;
    const float kBulletSpeed = 60.f;
    const float kBulletLifetime = 2.f;
    std::vector<Bullet> bullets(kMaxBullets);
    Mesh* bulletMesh = assets.createSphere("bullet", 0.025f, 5, 6);
    Material* bulletMat = scene.create_material(1.f, 0.9f, 0.4f);
    bulletMat->unlit = true;
    for (Bullet& b : bullets)
    {
        b.visual = scene.root().create_child<MeshInstance>("bullet");
        b.visual->set_mesh(bulletMesh);
        b.visual->set_material(bulletMat);
        b.visual->visible = false;
    }
    int nextBullet = 0;
    float muzzleFlashTimer = 0.f;

    // ── crosshair: reticle projected from the actual bullet aim point in
    // world space, so the on-screen reticle always marks where the bullet
    // will go. The aim point is computed from the camera's forward vector
    // plus the aim offsets; the bullet is fired from the weapon to that
    // same aim point, so the crosshair and trajectory never diverge. ──
    gl::Batch hud;
    hud.Init();
    gl::Texture* crosshairTex = assets.loadTexture("crosshair", (std::string(kAssetDir) + "gfx/crosshaira.tga").c_str());
    const float kCrosshairSize = 24.f;
    const int kVirtualW = 1280, kVirtualH = 720; // matches render_fit's virtual resolution below
    const float kAimDistance = 50.f;            // world distance at which the bullet and camera ray meet
    bool firePending = false;

    scene.set_active_camera(cam);
    scene.ready();

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
            if (ev.type == SDL_KEYDOWN && ev.key.keysym.scancode == SDL_SCANCODE_ESCAPE) running = false;
            if (ev.type == SDL_KEYDOWN && ev.key.keysym.scancode == SDL_SCANCODE_F10)
            {
                int gw, gh;
                app.DrawableSize(&gw, &gh);
                app.gif.Toggle(gw, gh);
            }
            // fire: left mouse click queues one bullet. The actual fire direction
            // is resolved later in the frame using the then-current camera
            // (crosshair aim point for the gameplay cam, worldCam forward for
            // free-fly).
            if (ev.type == SDL_MOUSEBUTTONDOWN && ev.button.button == SDL_BUTTON_LEFT && weaponAttach)
            {
                firePending = true;
            }
            // always-on aim look (no click-hold) — clamped to a narrow cone
            // around playerYaw, not a free-look camera; see kAimYawLimit/
            // kAimPitchLimit above
            if (ev.type == SDL_KEYDOWN && ev.key.keysym.scancode == SDL_SCANCODE_TAB)
            {
                freeCam = !freeCam;
                scene.set_active_camera(freeCam ? worldCam : cam);
                printf("free cam: %s\n", freeCam ? "on" : "off");
            }
            // debug: L/P page which ONE surface of the level mesh gets a
            // wireframe box (dump_surfaces() at startup already printed
            // every index/bounds; this just confirms which index is which
            // piece on screen before any raycast-based picking exists)
            if (ev.type == SDL_KEYDOWN && (ev.key.keysym.scancode == SDL_SCANCODE_L ||
                                           ev.key.keysym.scancode == SDL_SCANCODE_P))
            {
                int count = (int)levelMesh->surfaces().size();
                if (count > 0)
                {
                    int step = (ev.key.keysym.scancode == SDL_SCANCODE_L) ? 1 : -1;
                    debugSurfaceIndex = ((debugSurfaceIndex + step) % count + count) % count;
                    renderer.set_debug_surface_index(level, debugSurfaceIndex);
                    const Surface& s = levelMesh->surfaces()[(size_t)debugSurfaceIndex];
                    printf("surface debug box: [%d] bounds=(%.2f,%.2f,%.2f)-(%.2f,%.2f,%.2f)\n",
                          debugSurfaceIndex, s.bounds.min.x, s.bounds.min.y, s.bounds.min.z,
                          s.bounds.max.x, s.bounds.max.y, s.bounds.max.z);
                }
            }
            if (!freeCam && ev.type == SDL_MOUSEMOTION)
            {
                aimYaw -= (float)ev.motion.xrel * 0.004f;
                aimPitch -= (float)ev.motion.yrel * 0.004f;
                if (aimYaw > kAimYawLimit) aimYaw = kAimYawLimit;
                if (aimYaw < -kAimYawLimit) aimYaw = -kAimYawLimit;
                if (aimPitch > kAimPitchLimit) aimPitch = kAimPitchLimit;
                if (aimPitch < -kAimPitchLimit) aimPitch = -kAimPitchLimit;
            }
            fly.handle(ev); // only updates yaw/pitch/looking state; harmless outside free-cam mode
           
           
        }
        if (!running) break;

        gl::u64 now = SDL_GetPerformanceCounter();
        float dt = (float)((double)(now - lastTicks) / (double)freq);
        lastTicks = now;
        if (dt > 0.1f) dt = 0.1f;

        int windowW, windowH;
        app.DrawableSize(&windowW, &windowH);


        bool isMoving = false;
        bool isTurning = false;
        bool run = false;
        {
            const Uint8* keys = SDL_GetKeyboardState(nullptr);
            const float kTurnRate = 2.2f; // rad/s
            if (keys[SDL_SCANCODE_D]) { playerYaw += kTurnRate * dt; isTurning = true; }
            if (keys[SDL_SCANCODE_A]) { playerYaw -= kTurnRate * dt; isTurning = true; }

            Vec3 fwd(sinf(playerYaw), 0.f, -cosf(playerYaw));
            run = keys[SDL_SCANCODE_LSHIFT] != 0;
            if (keys[SDL_SCANCODE_W])
            {
                playerPos += fwd * ((run ? 6.5f : 3.f) * dt);
                isMoving = true;
            }
            if (keys[SDL_SCANCODE_S])
            {
                playerPos -= fwd * ((run ? 6.5f : 3.f) * dt);
                isMoving = true;
            }
        }
        lower->set_position(playerPos);
        lower->set_rotation(Quaternion::FromAxisAngle(Vec3(0.f, 1.f, 0.f), -playerYaw) * kMeshForwardFix);
        const char* legsClip = isMoving ? (run ? "LEGS_RUN" : "LEGS_WALK")
                              : isTurning ? "LEGS_TURN"
                                          : "LEGS_IDLE";
        lower->play(legsClip);

        scene.update(dt); // also advances torso_attach/head_attach (TagAttachment::_update)

        if (muzzleFlashTimer > 0.f)
        {
            muzzleFlashTimer -= dt;
            if (muzzleFlashTimer <= 0.f && muzzleFlash)
                muzzleFlash->set_color(Vec4(1.f, 0.9f, 0.6f, 0.f));
        }

        // bullet pool: move each active bullet, raycast the segment it
        // just swept against the level's triangle soup (worldCollision) —
        // a per-frame short raycast, not one long cast at fire time, so a
        // slow-enough bullet can't tunnel through a thin wall between
        // frames. Hit -> decal + spark burst, deactivate. Otherwise ages
        // out via ttl.
        for (Bullet& b : bullets)
        {
            if (!b.active) continue;
            b.ttl -= dt;
            Vec3 newPos = b.pos + b.vel * dt;
            Vec3 seg = newPos - b.pos;
            float segLen = seg.length();
            bool hitSomething = false;
            if (segLen > 1e-4f)
            {
                Ray ray(b.pos, seg * (1.f / segLen));
                CollisionInfo hit;
                if (worldCollision.rayCast(ray, segLen, hit))
                {
                    emit_bullet_impact(decals, sparks, hit.point, hit.normal);
                    hitSomething = true;
                }
            }
            if (hitSomething || b.ttl <= 0.f)
            {
                b.active = false;
                b.visual->visible = false;
                continue;
            }
            b.pos = newPos;
            b.visual->set_position(b.pos);
        }

        if (!freeCam)
        {
            upper->set_rotation(Quaternion::FromAxisAngle(Vec3(0.f, 1.f, 0.f), aimYaw));

 
            camYaw += (playerYaw - camYaw) * (1.f - expf(-8.f * dt));

            Vec3 backDir(sinf(camYaw), 0.f, -cosf(camYaw));
            const float camDist = 2.2f, camHeight = 2.55f, shoulderOffset = 0.5f;
            Vec3 eye = playerPos + Vec3(0.f, camHeight, 0.f);
            Vec3 rightVec(cosf(camYaw), 0.f, sinf(camYaw));
            cam->set_position(eye - backDir * camDist + rightVec * shoulderOffset);
            Vec3 lookDir(sinf(camYaw) * cosf(aimPitch), sinf(aimPitch), -cosf(camYaw) * cosf(aimPitch));
            cam->look_at(eye + lookDir * 10.f);
        }
        else
        {
            fly.apply(worldCam, dt);
        }

        // Compute the shared aim point and its screen projection after the
        // camera is positioned, so the crosshair and any fired bullet use
        // the same world-space target.
        Vec3 aimPoint;
        float crosshairX = 0.f, crosshairY = 0.f;
        bool crosshairVisible = false;
        if (!freeCam && weaponAttach)
        {
            Vec3 camPos = cam->get_global_position();
            Vec3 camForward = cam->global_forward();
            Vec3 aimDir = Quaternion::FromAxisAngle(Vec3(0.f, 1.f, 0.f), aimYaw).rotateVector(camForward);
            aimPoint = camPos + aimDir * kAimDistance;
            crosshairVisible = cam->world_to_screen(aimPoint, kVirtualW, kVirtualH, crosshairX, crosshairY);
        }

        if (firePending && weaponAttach)
        {
            Vec3 fireDir;
            if (freeCam)
            {
                fireDir = worldCam->forward();
            }
            else
            {
                Vec3 weaponPos = weaponAttach->get_global_position();
                fireDir = (aimPoint - weaponPos).normalized();
            }
            Bullet& b = bullets[nextBullet];
            nextBullet = (nextBullet + 1) % kMaxBullets;
            b.pos = weaponAttach->get_global_position();
            b.vel = fireDir * kBulletSpeed;
            b.ttl = kBulletLifetime;
            b.active = true;
            b.visual->set_position(b.pos);
            b.visual->visible = true;

            if (muzzleFlash)
            {
                muzzleFlash->set_color(Vec4(1.f, 0.9f, 0.6f, 1.f));
                muzzleFlashTimer = 0.06f;
            }
            firePending = false;
        }

        renderer.render_fit(scene, windowW, windowH, kVirtualW, kVirtualH, ViewportFitMode::Letterbox);

        if (!freeCam && crosshairTex && crosshairVisible)
        {
            Mat4 ortho = Mat4::Ortho(0.f, (float)kVirtualW, (float)kVirtualH, 0.f, -1.f, 1.f);
            hud.SetProjection(ortho.x);
            gl::Renderer::SetDepthTest(false);
            gl::Renderer::SetBlend(true);
            gl::Renderer::SetBlendFactors(gl::BlendFactor::SRC_ALPHA, gl::BlendFactor::ONE_MINUS_SRC_ALPHA);
            hud.SetColor(255, 255, 255, 230);
            hud.Quad(*crosshairTex, crosshairX - kCrosshairSize * 0.5f, crosshairY - kCrosshairSize * 0.5f, kCrosshairSize,
                    kCrosshairSize, true, false);
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
    hud.Release();
    scene.release_gpu();
    assets.release();
    renderer.release();
    app.Destroy();
    return 0;
}
