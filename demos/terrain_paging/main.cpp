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
#include <scene/LensFlareNode.hpp>
#include <scene/AssetManager.hpp>
#include <algorithm>

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
    printf("controls: WASD/QE + mouse | LSHIFT fast | C colors | R raise/F dig | F9 stats | F10 gif\n");

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
    if (getenv("COREGL_STATS")) renderer.set_show_stats(true); // F9 toggles too

    Scene scene;
    assets::AssetManager& assets = assets::AssetManager::instance();
    gl::Texture* grass = assets.loadTexture("l_grass", "assets/terrain/grass.jpg");
    gl::Texture* ground = assets.loadTexture("l_ground", "assets/textures/terrain/Ground37_diffspec.png");
    gl::Texture* rock = assets.loadTexture("l_rock", "assets/textures/terrain/Rock20_diffspec.png");
    gl::Texture* sand = assets.loadTexture("l_sand", "assets/terrain/sand.jpg");
    gl::Texture* snow = assets.loadTexture("l_snow", "assets/terrain/snow.jpg");

    TerrainPagingNode* terrain = scene.root().create_child<TerrainPagingNode>("world");
    terrain->set_page_size(257) // mobile/web: 129, desktop: 257
        .set_cell_size(512.f)
        .set_load_radius(1200.f)
        .set_hold_radius(1600.f)
        .set_height_function(worldHeight);
    // bounded world instead of infinite: terrain->set_extent(-4, -4, 3, 3);

    // Ogre-style splatting: base grass; ground/rock/sand/snow mixed on top
    // by height and slope. worldSize = meters one texture repeat covers.
    terrain->set_layer(0, grass, 12.f)
        .set_layer(1, ground, 14.f)
        .set_layer(2, rock, 20.f)
        .set_layer(3, sand, 10.f)
        .set_layer(4, snow, 16.f)
        .set_blend_function(
            [](float, float, float h, float slope, float w[4])
            {
                w[1] = std::min(std::max((slope - 0.10f) * 6.f, 0.f), 1.f);  // rock on slopes
                w[2] = std::min(std::max((-2.f - h) * 0.20f, 0.f), 1.f);     // sand low
                w[3] = std::min(std::max((h - 24.f) * 0.12f, 0.f), 1.f);     // snow high
                w[0] = std::min(std::max((slope - 0.04f) * 4.f, 0.f), 0.6f); // ground patches
            });

    Camera3D* camera = scene.root().create_child<Camera3D>("fly");
    camera->set_perspective(60.f, 0.5f, 2000.f);
    camera->set_position(0.f, 60.f, 0.f);


   
    LensFlareNode* flare = scene.root().create_child<LensFlareNode>("sun_flare");
    
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
                if (ev.key.keysym.sym == SDLK_F9)
                    renderer.set_show_stats(!renderer.show_stats());
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

        // terrain editing: hold R to raise / F to dig at the point the
        // camera looks at (ray-marched against the edited heightfield)
        {
            const Uint8* keys = SDL_GetKeyboardState(nullptr);
            float dir = keys[SDL_SCANCODE_R] ? 1.f : (keys[SDL_SCANCODE_F] ? -1.f : 0.f);
            // automated check: raise for 60 frames, then dig 60 (no keyboard)
            if (getenv("COREGL_EDIT_TEST") && frame < 120) dir = frame < 60 ? 1.f : -0.5f;
            if (dir != 0.f)
            {
                Vec3 o = camera->get_position();
                Vec3 fwd = camera->forward();
                for (float t = 2.f; t < 500.f; t += 2.f)
                {
                    Vec3 p = o + fwd * t;
                    if (p.y <= terrain->height_at(p.x, p.z))
                    {
                        terrain->modify_height(p.x, p.z, 22.f, dir * 20.f * dt);
                        break;
                    }
                }
            }
        }
        scene.update(dt);

        int w, h;
        app.DrawableSize(&w, &h);
        terrain->set_lod_camera(60.f, h); // drives the de Boer LOD formula
        renderer.render(scene, w, h);
        perf.tick(frame, renderer.last_item_count(), dt);
        app.EndFrame();

        ++frame;
        if (maxFrames > 0 && frame >= maxFrames) running = false;
    }

    printf("frames: %d | pages resident: %d | pages built total: %d\n", frame,
           terrain->page_count(), terrain->pages_built_total());
    {
        int histo[8] = {};
        for (int cy = -8; cy <= 8; ++cy)
            for (int cx = -8; cx <= 8; ++cx)
            {
                int l = terrain->lod_of_page(cx, cy);
                if (l >= 0 && l < 8) ++histo[l];
            }
        printf("lod histogram:");
        for (int l = 0; l < 8; ++l)
            if (histo[l]) printf("  L%d=%d", l, histo[l]);
        printf("\n");
    }
    scene.release_gpu();
    assets.release();
    renderer.release();
    app.Destroy();
    return 0;
}
