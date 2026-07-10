// TerrainPagingNode demo: Ogre-style paged world. Pages stream in/out around
// the camera (Grid2D load/hold radius, 1 build per frame). Fly far in any
// direction — the world never ends and memory stays bounded.
//
// Controls: WASD/QE + mouse, LSHIFT fast, C debug page colors, F10 gif, ESC.

#include "demo_app.hpp"
#include "demo_fly.hpp"
#include "demo_perf.hpp"
#include <scene/Scene.hpp>
#include <scene/SceneRenderer.hpp>
#include <scene/TerrainPagingNode.hpp>
#include <scene/AssetManager.hpp>

// rolling hills + ridges — same character as the other terrain demos
static float worldHeight(float x, float z)
{
    float h = sinf(x * 0.008f) * cosf(z * 0.008f) * 28.f;
    h += sinf(x * 0.03f + 1.7f) * cosf(z * 0.025f) * 7.f;
    h += sinf(x * 0.11f) * sinf(z * 0.13f) * 1.2f;
    return h;
}

int main(int argc, char** argv)
{
    const int maxFrames = (argc > 1) ? atoi(argv[1]) : 0;

    DemoApp app;
    if (!app.Create("coregl - terrain paging")) return 1;
    printf("controls: WASD/QE + mouse | LSHIFT fast | C page colors | F10 gif\n");

    SceneRenderer renderer;
    if (!renderer.init())
    {
        fprintf(stderr, "renderer init failed\n");
        app.Destroy();
        return 1;
    }
    renderer.set_light_dir(Vec3(0.4f, -0.7f, 0.3f));
    renderer.set_sky_enabled(true);
    renderer.enable_shadows();

    Scene scene;
    assets::AssetManager& assets = assets::AssetManager::instance();
    gl::Texture* grass = assets.loadTexture("grass", "assets/terrain/terrain-texture.jpg");
    gl::Texture* detail = assets.loadTexture("detail", "assets/terrain/detailmap3.jpg");

    TerrainPagingNode* terrain = scene.root().create_child<TerrainPagingNode>("world");
    terrain->set_page_size(257) // mobile/web: 129, desktop: 257
        .set_cell_size(256.f)
        .set_load_radius(600.f)
        .set_hold_radius(850.f)
        .set_height_function(worldHeight)
        .set_texture(grass, detail, 24.f);

    Camera3D* camera = scene.root().create_child<Camera3D>("fly");
    camera->set_perspective(60.f, 0.5f, 2000.f);
    camera->set_position(0.f, 60.f, 0.f);

    scene.set_active_camera(camera);
    scene.ready();

    FlyCam fly;
    fly.speed = 60.f;
    gl::u64 lastTicks = SDL_GetPerformanceCounter();
    const gl::u64 freq = SDL_GetPerformanceFrequency();

    PerfPrinter perf;
    int frame = 0;
    bool running = true, debugColors = false;
    while (running)
    {
        SDL_Event ev;
        while (SDL_PollEvent(&ev))
        {
            if (ev.type == SDL_QUIT) running = false;
            if (ev.type == SDL_KEYDOWN)
            {
                if (ev.key.keysym.sym == SDLK_ESCAPE) running = false;
                if (ev.key.keysym.sym == SDLK_c)
                {
                    debugColors = !debugColors;
                    terrain->set_debug_colors(debugColors);
                }
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

    printf("frames: %d | pages resident: %d | pages built total: %d\n", frame,
           terrain->page_count(), terrain->pages_built_total());
    scene.release_gpu();
    assets.release();
    renderer.release();
    app.Destroy();
    return 0;
}
