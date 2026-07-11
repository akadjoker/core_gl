// Primitive mesh demo: cube/sphere/cylinder/cone, all owned by the
// AssetManager (single point of truth for mesh memory) — the game only
// holds pointers, release() at shutdown frees everything.
//
// Controls: WASD/QE + mouse, LSHIFT fast, F10 gif, ESC quit.

#include "demo_app.hpp"
#include "demo_fly.hpp"
#include <scene/Scene.hpp>
#include <scene/SceneRenderer.hpp>
#include <scene/Material.hpp>
#include <scene/MeshInstance.hpp>
#include <scene/AssetManager.hpp>
#include <vector>
#include <cmath>

int main(int argc, char** argv)
{
    const int maxFrames = (argc > 1) ? atoi(argv[1]) : 0;

    DemoApp app;
    if (!app.Create("coregl - primitives")) return 1;

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

    Mesh* ground = assets.createPlane("ground", 60.f, 60.f, 6.f);
    Mesh* cube = assets.createCube("cube", 2.f, 2.f, 2.f);
    Mesh* sphere = assets.createSphere("sphere", 1.2f);
    Mesh* cyl = assets.createCylinder("cylinder", 0.8f, 2.5f);
    Mesh* cone = assets.createCone("cone", 1.f, 2.f);
    Mesh* capsule = assets.createCapsule("capsule", 0.6f, 1.2f);

    // Irrlicht-style hills ground, off to the side: a subdivided plane
    // displaced per-vertex by a height function, normals recomputed
    Mesh* hills = assets.createHillsPlane(
        "hills", 24.f, 24.f, 24, 24,
        [](float x, float z) -> float
        { return sinf(x * 0.35f) * cosf(z * 0.35f) * 1.4f; },
        4.f);

    // brute-force heightfield mesh from a raw array (no paging, single
    // static mesh — good for a small island or a prop-sized patch)
    Mesh* patch;
    {
        const int n = 33;
        std::vector<float> h((size_t)n * n);
        for (int j = 0; j < n; ++j)
            for (int i = 0; i < n; ++i)
                h[(size_t)j * n + i] =
                    sinf((float)i * 0.4f) * cosf((float)j * 0.4f) * 1.2f +
                    ((i - n / 2) * (i - n / 2) + (j - n / 2) * (j - n / 2)) * -0.01f;
        patch = assets.createHeightfield("patch", h.data(), n, n, 0.6f, 3.f);
    }

    Material* groundMat = scene.create_material(0.5f, 0.5f, 0.5f);
    MeshInstance* groundInst = scene.root().create_child<MeshInstance>("ground");
    groundInst->set_mesh(ground);
    groundInst->set_material(groundMat);

    struct { Mesh* mesh; const char* name; float x; Vec3 color; } shapes[] = {
        {cube, "cube", -8.f, Vec3(0.8f, 0.3f, 0.3f)},
        {sphere, "sphere", -4.5f, Vec3(0.3f, 0.8f, 0.3f)},
        {cyl, "cylinder", -1.f, Vec3(0.3f, 0.3f, 0.8f)},
        {cone, "cone", 2.5f, Vec3(0.8f, 0.7f, 0.2f)},
        {capsule, "capsule", 6.f, Vec3(0.7f, 0.3f, 0.7f)},
    };
    for (auto& s : shapes)
    {
        Material* mat = scene.create_material(s.color.x, s.color.y, s.color.z);
        MeshInstance* inst = scene.root().create_child<MeshInstance>(s.name);
        inst->set_mesh(s.mesh);
        inst->set_material(mat);
        inst->set_position(s.x, 1.5f, 0.f);
    }

    Material* hillsMat = scene.create_material(0.4f, 0.55f, 0.3f);
    MeshInstance* hillsInst = scene.root().create_child<MeshInstance>("hills");
    hillsInst->set_mesh(hills);
    hillsInst->set_material(hillsMat);
    hillsInst->set_position(-24.f, 0.02f, -18.f); // clear of the flat ground

    Material* patchMat = scene.create_material(0.55f, 0.45f, 0.3f);
    MeshInstance* patchInst = scene.root().create_child<MeshInstance>("patch");
    patchInst->set_mesh(patch);
    patchInst->set_material(patchMat);
    patchInst->set_position(14.f, 0.02f, -18.f);

    // second createCube call with the same name must return the SAME mesh
    // (no duplicate upload) — proof the manager dedupes by name
    Mesh* cubeAgain = assets.createCube("cube", 2.f, 2.f, 2.f);
    printf("dedupe check: %s | meshCount=%u\n", cubeAgain == cube ? "OK (same ptr)" : "FAIL",
           assets.meshCount());

    Camera3D* camera = scene.root().create_child<Camera3D>("fly");
    camera->set_perspective(55.f, 0.1f, 200.f);
    camera->set_position(0.f, 4.f, 14.f);

    scene.set_active_camera(camera);
    scene.ready();

    FlyCam fly;
    fly.speed = 12.f;
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
            if (ev.type == SDL_KEYDOWN)
            {
                if (ev.key.keysym.sym == SDLK_ESCAPE) running = false;
                if (ev.key.keysym.sym == SDLK_F10)
                {
                    int gw, gh;
                    app.DrawableSize(&gw, &gh);
                    app.gif.Toggle(gw, gh);
                }
            }
            fly.handle(ev);
        }
        if (!running) break;

        gl::u64 now = SDL_GetPerformanceCounter();
        float dt = (float)((double)(now - lastTicks) / (double)freq);
        lastTicks = now;
        if (dt > 0.1f) dt = 0.1f;

        fly.apply(camera, dt);
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
    assets.release(); // frees every primitive mesh created above
    renderer.release();
    app.Destroy();
    return 0;
}
