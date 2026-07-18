// Autodesk 3D Studio (.3ds) loading smoke test — first step towards porting
// the Hoverjet Racing lua demo (tmp/apocalyx/demos/HoverjetRacing.lua),
// whose props/ships ship as .3ds + .jpg pairs (tmp/apocalyx/demos/
// HoverjetRacing.dat_FILES/astro*.3ds). Loads one such prop through
// AssetManager::load_3ds_mesh and spins it in front of the camera.
//
// Controls: WASD/QE + mouse, LSHIFT fast, F10 gif, ESC quit.

#include "demo_app.hpp"
#include "demo_fly.hpp"
#include <scene/AssetManager.hpp>
#include <scene/Camera3D.hpp>
#include <scene/Material.hpp>
#include <scene/Mesh.hpp>
#include <scene/MeshInstance.hpp>
#include <scene/Scene.hpp>
#include <scene/SceneRenderer.hpp>
#include <chrono>
#include <vector>

int main(int argc, char** argv)
{
    const int maxFrames = (argc > 1) ? atoi(argv[1]) : 0;

    DemoApp app;
    if (!app.Create("coregl - 3ds loader")) return 1;
    printf("controls: WASD/QE + mouse | LSHIFT fast | F10 gif | ESC\n");

    SceneRenderer renderer;
    if (!renderer.init())
    {
        fprintf(stderr, "renderer init failed\n");
        app.Destroy();
        return 1;
    }
    renderer.set_light_dir(Vec3(0.4f, -0.8f, 0.3f));
    renderer.set_sky_enabled(true);
    renderer.enable_shadows();

    Scene scene;
    assets::AssetManager& assets = assets::AssetManager::instance();

    auto t0 = std::chrono::steady_clock::now();
    std::vector<Material*> materials;
    Mesh* astro = assets.load_3ds_mesh("astro1", "demos/format3ds/assets/astro1.3ds", materials);
    auto t1 = std::chrono::steady_clock::now();

    if (!astro)
    {
        fprintf(stderr, "3ds load failed (run from repo root)\n");
        app.Destroy();
        return 1;
    }

    const double loadMs = std::chrono::duration<double, std::milli>(t1 - t0).count();
    printf("astro1.3ds: %u surfaces, %u verts, %u indices, %u materials | load %.1fms\n",
           (unsigned)astro->surfaces().size(), astro->vertex_count(), astro->index_count(),
           (unsigned)materials.size(), loadMs);

    MeshInstance* prop = scene.root().create_child<MeshInstance>("astro1");
    prop->set_mesh(astro);
    prop->set_materials(materials);
    prop->set_position(0.f, 0.f, 0.f);

    Camera3D* camera = scene.root().create_child<Camera3D>("fly");
    camera->set_perspective(60.f, 0.1f, 500.f);
    const BoundingBox& b = astro->bounds();
    Vec3 center = (b.min + b.max) * 0.5f;
    float radius = (b.max - b.min).length() * 0.5f + 1.f;
    camera->set_position(center.x, center.y + radius * 0.4f, center.z + radius * 2.5f);

    scene.set_active_camera(camera);
    scene.ready();

    FlyCam fly;
    fly.speed = radius * 2.f;
    gl::u64 lastTicks = SDL_GetPerformanceCounter();
    const gl::u64 freq = SDL_GetPerformanceFrequency();

    float spin = 0.f;
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
        spin += dt * 20.f;
        prop->set_rotation(Quaternion::FromAxisAngle(Vec3(0.f, 1.f, 0.f), spin * 3.14159265f / 180.f));
        scene.update(dt);

        int w, h;
        app.DrawableSize(&w, &h);
        renderer.render(scene, w, h);
        app.EndFrame();

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
