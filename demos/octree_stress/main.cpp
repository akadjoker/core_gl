// SceneOctree stress test: scatters thousands of primitive MeshInstances over
// a large area to prove the object-level octree (scene/SceneOctree.hpp) cuts
// collect() cost vs. the plain Node-tree walk, without changing what's drawn.
//
// Controls: WASD/QE + mouse, LSHIFT fast, O toggle octree culling,
// G toggle octree debug boxes, F9 stats, F10 gif, ESC quit.

#include "demo_app.hpp"
#include "demo_fly.hpp"
#include <scene/Scene.hpp>
#include <scene/SceneRenderer.hpp>
#include <scene/Material.hpp>
#include <scene/MeshInstance.hpp>
#include <scene/AssetManager.hpp>
#include <chrono>
#include <cstdlib>

int main(int argc, char** argv)
{
    const int maxFrames = (argc > 1) ? atoi(argv[1]) : 0;
    const int gridN = getenv("COREGL_GRID") ? atoi(getenv("COREGL_GRID")) : 140; // ~19600 instances

    DemoApp app;
    if (!app.Create("coregl - octree stress")) return 1;

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

    Mesh* cube = assets.createCube("cube", 1.6f, 1.6f, 1.6f);
    Mesh* sphere = assets.createSphere("sphere", 1.0f);
    Mesh* cyl = assets.createCylinder("cylinder", 0.7f, 2.0f);

    Material* matA = scene.create_material(0.75f, 0.35f, 0.3f);
    Material* matB = scene.create_material(0.3f, 0.7f, 0.4f);
    Material* matC = scene.create_material(0.35f, 0.45f, 0.8f);

    const float spacing = 14.f; // world units between instances
    srand(1234);
    int count = 0;
    for (int gz = 0; gz < gridN; ++gz)
    {
        for (int gx = 0; gx < gridN; ++gx)
        {
            float jitter_x = ((rand() % 1000) / 1000.f - 0.5f) * spacing * 0.6f;
            float jitter_z = ((rand() % 1000) / 1000.f - 0.5f) * spacing * 0.6f;
            float x = (gx - gridN * 0.5f) * spacing + jitter_x;
            float z = (gz - gridN * 0.5f) * spacing + jitter_z;

            int pick = (gx + gz * 7) % 3;
            Mesh* mesh = pick == 0 ? cube : (pick == 1 ? sphere : cyl);
            Material* mat = pick == 0 ? matA : (pick == 1 ? matB : matC);

            MeshInstance* inst = scene.root().create_child<MeshInstance>("inst");
            inst->set_mesh(mesh);
            inst->set_material(mat);
            inst->set_position(x, 1.f, z);
            ++count;
        }
    }
    printf("octree_stress: %d instances scattered over %.0fx%.0f world units\n", count,
           gridN * spacing, gridN * spacing);

    Camera3D* camera = scene.root().create_child<Camera3D>("fly");
    camera->set_perspective(60.f, 0.1f, 400.f);
    camera->set_position(0.f, 6.f, 20.f);

    scene.set_active_camera(camera);
    scene.ready();

    renderer.build_spatial_index(scene);
    renderer.set_use_spatial_index(!getenv("COREGL_NO_OCTREE"));
    if (getenv("COREGL_SHOW_OCTREE")) renderer.set_show_octree_debug(true);
    printf("controls: WASD/QE + mouse | LSHIFT fast | O toggle octree culling | "
           "G octree debug boxes | F9 stats | F10 gif | ESC quit\n");

    FlyCam fly;
    fly.speed = 30.f;
    gl::u64 lastTicks = SDL_GetPerformanceCounter();
    const gl::u64 freq = SDL_GetPerformanceFrequency();

    if (getenv("COREGL_GIF_AUTOSTART"))
    {
        int gw, gh;
        app.DrawableSize(&gw, &gh);
        app.gif.Toggle(gw, gh);
    }

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
                if (ev.key.keysym.sym == SDLK_F9)
                    renderer.set_show_stats(!renderer.show_stats());
                if (ev.key.keysym.sym == SDLK_F10)
                {
                    int gw, gh;
                    app.DrawableSize(&gw, &gh);
                    app.gif.Toggle(gw, gh);
                }
                if (ev.key.keysym.sym == SDLK_o)
                {
                    bool on = !renderer.use_spatial_index();
                    renderer.set_use_spatial_index(on);
                    printf("octree culling: %s\n", on ? "ON" : "off (naive tree walk)");
                }
                if (ev.key.keysym.sym == SDLK_g)
                {
                    static bool on = false;
                    on = !on;
                    renderer.set_show_octree_debug(on);
                    printf("octree debug boxes: %s\n", on ? "ON" : "off");
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

        auto t0 = std::chrono::steady_clock::now();
        renderer.render(scene, w, h);
        auto t1 = std::chrono::steady_clock::now();
        app.EndFrame();

        if (frame % 60 == 0)
        {
            double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
            printf("frame %d: render %.2fms | collect %.4fms | items: %d | octree: %s\n", frame,
                   ms, renderer.last_collect_ms(), renderer.last_item_count(),
                   renderer.use_spatial_index() ? "on" : "off");
        }

        ++frame;
        if (maxFrames > 0 && frame >= maxFrames) running = false;
    }

    if (getenv("COREGL_GIF_AUTOSTART")) app.gif.Toggle(0, 0);
    printf("frames: %d\n", frame);
    scene.release_gpu();
    assets.release();
    renderer.release();
    app.Destroy();
    return 0;
}
