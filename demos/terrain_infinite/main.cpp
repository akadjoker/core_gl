// InfiniteTerrainNode demo: endless terrain tiling a base heightmap around
// the camera. Patches build on demand with distance LOD (skirts hide the
// seams) and an LRU cache evicts what you leave behind — fly in one
// direction forever.
//
// Controls: WASD/QE + mouse, LSHIFT fast, F10 gif, ESC quit.

#include "demo_app.hpp"
#include "demo_fly.hpp"
#include "demo_perf.hpp"
#include <scene/Scene.hpp>
#include <scene/SceneRenderer.hpp>
#include <scene/Material.hpp>
#include <scene/InfiniteTerrainNode.hpp>
#include <scene/AssetManager.hpp>

int main(int argc, char** argv)
{
    const int maxFrames = (argc > 1) ? atoi(argv[1]) : 0;

    DemoApp app;
    if (!app.Create("coregl - infinite terrain")) return 1;
    printf("controls: WASD/QE + mouse | LSHIFT fast | F10 gif\n");

    SceneRenderer renderer;
    if (!renderer.init())
    {
        fprintf(stderr, "renderer init failed\n");
        app.Destroy();
        return 1;
    }
    renderer.set_light_dir(Vec3(0.45f, -0.7f, -0.35f));
    renderer.set_sky_enabled(true);

    Scene scene;
    assets::AssetManager& assets = assets::AssetManager::instance();

    InfiniteTerrainNode* terrain = scene.root().create_child<InfiniteTerrainNode>("infinite");
    terrain->load_base_heightmap("assets/terrain/terrain-heightmap.png", 60.f);
    terrain->configure(6, 33, 64.f, 1024.f);
    Material* mat = scene.create_material();
    mat->diffuse = assets.loadTexture("ground", "assets/terrain/terrain-texture.jpg");
    mat->detail = assets.loadTexture("detail", "assets/terrain/detailmap3.jpg");
    mat->detail_scale = 60.f;
    terrain->set_material(mat);

    Camera3D* camera = scene.root().create_child<Camera3D>("fly");
    camera->set_perspective(55.f, 0.1f, 900.f);
    camera->set_position(0.f, terrain->height_at(0.f, 0.f) + 25.f, 0.f);

    scene.set_active_camera(camera);
    scene.ready();

    FlyCam fly;
    fly.speed = 40.f;
    gl::u64 lastTicks = SDL_GetPerformanceCounter();
    const gl::u64 freq = SDL_GetPerformanceFrequency();

    PerfPrinter perf;
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

        // the terrain follows the camera: patches appear ahead, cache
        // evicts behind
        terrain->set_camera_position(camera->get_position());

        scene.update(dt);
        int w, h;
        app.DrawableSize(&w, &h);
        renderer.render(scene, w, h);
        perf.tick(frame, renderer.last_item_count(), dt);
        app.EndFrame();

        ++frame;
        if (maxFrames > 0 && frame >= maxFrames) running = false;
    }

    printf("frames: %d | items/frame: %d\n", frame, renderer.last_item_count());
    scene.release_gpu();
    assets.release();
    renderer.release();
    app.Destroy();
    return 0;
}
