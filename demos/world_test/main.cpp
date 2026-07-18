// World demo: a small, fully self-contained collision system
// (scene/World.hpp — no dependency on MeshCollision.hpp), modeled on
// Blitz3D's own World/UpdateWorld — move the cube with WASD (plain
// set_position, no velocity anywhere, same as Blitz3D's
// MoveEntity/TranslateEntity) and world.update(dt) automatically slides
// it (sphere-vs-mesh, one of World's three collision methods) along
// level.ms3d's walls and floor.
//
// Gravity/jump follow the same rule as collision_sliding.bb: World owns
// neither — jumpForce is a plain local variable, integrated into the
// cube's Y position by hand every frame (exactly like the .bb's
// `TranslateEntity player, 0, jump_force-gravity, 0`), and "on the
// ground" is read straight from World's own last_contact() normal
// (World's equivalent of Blitz3D's CollisionNY).
//
// Controls: WASD move the cube | Space jump | mouse-drag orbit the
// camera | 1/2/3 switch response (Stop/Slide/SlideXZ) | F10 gif | ESC quit.

#include "demo_app.hpp"
#include <scene/AssetManager.hpp>
#include <scene/Camera3D.hpp>
#include <scene/Material.hpp>
#include <scene/MeshInstance.hpp>
#include <scene/Scene.hpp>
#include <scene/SceneRenderer.hpp>
#include <scene/World.hpp>

int main(int argc, char** argv)
{
    const int maxFrames = (argc > 1) ? atoi(argv[1]) : 0;

    DemoApp app;
    if (!app.Create("coregl - world test")) return 1;

    SceneRenderer renderer;
    if (!renderer.init())
    {
        fprintf(stderr, "renderer init failed\n");
        app.Destroy();
        return 1;
    }
    renderer.set_light_dir(Vec3(0.4f, -0.7f, 0.3f));
    renderer.set_sky_enabled(true);

    Scene scene;
    assets::AssetManager& assets = assets::AssetManager::instance();

    // ── the level: same MS3D file the ms3d_test demo proved working ──
    std::vector<Material*> levelMats;
    Mesh* levelMesh =
        assets.load_ms3d_mesh("world_level", "assets/models/ms3d/level.ms3d", levelMats);
    if (!levelMesh)
    {
        fprintf(stderr, "level.ms3d load failed\n");
        app.Destroy();
        return 1;
    }
    MeshInstance* levelInst = scene.root().create_child<MeshInstance>("level");
    levelInst->set_mesh(levelMesh);
    levelInst->set_materials(levelMats);
    levelInst->set_scale(0.5f);

    // ── the body: a small cube, driven by plain WASD position changes ──
    Mesh* cubeMesh = assets.createCube("world_cube", 2.f, 2.f, 2.f);
    Material* cubeMat = scene.create_material(0.9f, 0.3f, 0.2f);
    MeshInstance* cubeInst = scene.root().create_child<MeshInstance>("cube");
    cubeInst->set_mesh(cubeMesh);
    cubeInst->set_material(cubeMat);
    cubeInst->set_position(0.f, 5.f, 30.f);

    World world;
    world.add_static_mesh(levelMesh, levelInst);
    // capsule mover (radius 1, total height 3 — taller than a plain sphere
    // would need, standard character-controller shape) exercises the new
    // capsule-vs-mesh path against level.ms3d's floor/walls
    int cubeBody = world.add_capsule_body(cubeInst, 1.f, 3.f, CollisionResponse::SlideXZ);

    Camera3D* camera = scene.root().create_child<Camera3D>("cam");
    camera->set_perspective(60.f, 0.1f, 300.f);

    scene.set_active_camera(camera);
    scene.ready();

    // simple orbit camera around the cube — mouse-drag to look, no fly
    float orbitYaw = 0.f, orbitPitch = 0.35f, orbitDist = 18.f;
    bool dragging = false;

    gl::u64 lastTicks = SDL_GetPerformanceCounter();
    const gl::u64 freq = SDL_GetPerformanceFrequency();

    const char* responseNames[] = {"Stop", "Slide", "SlideXZ"};
    CollisionResponse response = CollisionResponse::SlideXZ;

    // ── gravity/jump: entirely game-side state, same as the Blitz3D demo
    // — World never sees these, only the resulting position change ──
    const float gravity = 30.f;   // units/s^2
    const float jumpSpeed = 12.f; // units/s, upward, on takeoff
    float jumpForce = 0.f;        // current vertical speed (game-owned, not World's)
    bool onGround = false;

    int frame = 0;
    bool running = true;
    while (running)
    {
        SDL_Event ev;
        while (SDL_PollEvent(&ev))
        {
            if (ev.type == SDL_QUIT) running = false;
            if (ev.type == SDL_MOUSEBUTTONDOWN && ev.button.button == SDL_BUTTON_LEFT)
                dragging = true;
            if (ev.type == SDL_MOUSEBUTTONUP && ev.button.button == SDL_BUTTON_LEFT)
                dragging = false;
            if (ev.type == SDL_MOUSEMOTION && dragging)
            {
                orbitYaw -= (float)ev.motion.xrel * 0.006f;
                orbitPitch -= (float)ev.motion.yrel * 0.006f;
                if (orbitPitch > 1.2f) orbitPitch = 1.2f;
                if (orbitPitch < -1.0f) orbitPitch = -1.0f;
            }
            if (ev.type == SDL_MOUSEWHEEL)
            {
                orbitDist -= (float)ev.wheel.y * 1.5f;
                if (orbitDist < 4.f) orbitDist = 4.f;
                if (orbitDist > 60.f) orbitDist = 60.f;
            }
            if (ev.type == SDL_KEYDOWN)
            {
                if (ev.key.keysym.sym == SDLK_ESCAPE) running = false;
                if (ev.key.keysym.sym == SDLK_F10)
                {
                    int gw, gh;
                    app.DrawableSize(&gw, &gh);
                    app.gif.Toggle(gw, gh);
                }
                if (ev.key.keysym.sym == SDLK_1)
                {
                    response = CollisionResponse::Stop;
                    world.set_response(cubeBody, response);
                    printf("response: %s\n", responseNames[(int)response]);
                }
                if (ev.key.keysym.sym == SDLK_2)
                {
                    response = CollisionResponse::Slide;
                    world.set_response(cubeBody, response);
                    printf("response: %s\n", responseNames[(int)response]);
                }
                if (ev.key.keysym.sym == SDLK_3)
                {
                    response = CollisionResponse::SlideXZ;
                    world.set_response(cubeBody, response);
                    printf("response: %s\n", responseNames[(int)response]);
                }
                if (ev.key.keysym.sym == SDLK_SPACE && onGround)
                {
                    jumpForce = jumpSpeed;
                    onGround = false;
                }
            }
        }
        if (!running) break;

        gl::u64 now = SDL_GetPerformanceCounter();
        float dt = (float)((double)(now - lastTicks) / (double)freq);
        lastTicks = now;
        if (dt > 0.1f) dt = 0.1f;


        const Uint8* keys = SDL_GetKeyboardState(nullptr);
        float speed = 8.f * dt;
        Vec3 pos = cubeInst->get_position();
        if (keys[SDL_SCANCODE_W]) pos.z -= speed;
        if (keys[SDL_SCANCODE_S]) pos.z += speed;
        if (keys[SDL_SCANCODE_A]) pos.x -= speed;
        if (keys[SDL_SCANCODE_D]) pos.x += speed;

        // gravity/jump: plain position integration, game-side, same as
        // collision_sliding.bb's `TranslateEntity player, 0,
        // jump_force-gravity, 0` — World only ever sees the resulting
        // position, never jumpForce itself
        jumpForce -= gravity * dt;
        if (jumpForce < -20.f) jumpForce = -20.f; // terminal velocity, same idea as the .bb's clamp
        pos.y += jumpForce * dt;
        cubeInst->set_position(pos);

        world.update(dt);

        // on the ground? same idea as Blitz3D's CollisionNY: the last
        // contact's surface normal points mostly straight up
        onGround = world.last_contact(cubeBody).hit && world.last_contact(cubeBody).normal.y > 0.5f;
        if (onGround && jumpForce < 0.f) jumpForce = 0.f; // landed — stop accumulating fall speed

        // camera orbits the cube's (corrected) position
        Vec3 cubePos = cubeInst->get_position();
        Vec3 offset(cosf(orbitPitch) * sinf(orbitYaw) * orbitDist, sinf(orbitPitch) * orbitDist,
                   cosf(orbitPitch) * cosf(orbitYaw) * orbitDist);
        camera->set_position(cubePos + offset);
        camera->look_at(cubePos);

        scene.update(dt);

        int w, h;
        app.DrawableSize(&w, &h);
        renderer.render(scene, w, h);
        app.EndFrame();

        ++frame;
        if (maxFrames > 0 && frame >= maxFrames) running = false;
    }

    printf("frames: %d\n", frame);
    scene.release_gpu();
    assets.release();
    renderer.release();
    app.Destroy();
    return 0;
}
