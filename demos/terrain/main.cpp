// TerrainNode demo: block-based heightmap terrain from a real image
// (assets/terrain/island-height.jpg) with its color texture, under CSM +
// procedural sky. Blocks cull per-surface; the camera can walk the ground
// (hold SPACE) using the CPU height query.
//
// Controls: WASD/QE + mouse, LSHIFT fast, SPACE walk mode, C cascade tint,
//           F10 gif, ESC quit. Run from the repo root.

#include "demo_app.hpp"
#include "demo_fly.hpp"
 
#include <scene/Scene.hpp>
#include <scene/SceneRenderer.hpp>
#include <scene/Material.hpp>
#include <scene/TerrainNode.hpp>
#include <scene/AssetManager.hpp>
#include <vector>

int main(int argc, char** argv)
{
    const int maxFrames = (argc > 1) ? atoi(argv[1]) : 0;

    DemoApp app;
    if (!app.Create("coregl - terrain (blocks)")) return 1;
    printf("controls: WASD/QE + mouse | LSHIFT fast | SPACE walk | C cascade tint | F10 gif\n");

    SceneRenderer renderer;
    if (!renderer.init() || !renderer.enable_shadows(4, 2048, 400.f))
    {
        fprintf(stderr, "renderer init failed\n");
        app.Destroy();
        return 1;
    }
    renderer.set_light_dir(Vec3(0.45f, -0.7f, -0.35f));
    renderer.set_sky_enabled(true);

    Scene scene;
    assets::AssetManager& assets = assets::AssetManager::instance();

    TerrainNode* terrain = scene.root().create_child<TerrainNode>("terrain");
    if (!terrain->load_heightmap("assets/terrain/island-height.jpg", 400.f, 45.f, 400.f))
    {
        // never abort over a missing asset: procedural stand-in hills
        std::vector<float> hs((size_t)256 * 256);
        for (int z = 0; z < 256; ++z)
            for (int x = 0; x < 256; ++x)
                hs[(size_t)z * 256 + x] = 0.5f + 0.3f * sinf(x * 0.05f) * cosf(z * 0.06f);
        terrain->build(hs.data(), 256, 400.f, 45.f, 400.f);
    }
    Material* mat = scene.create_material();
    mat->diffuse = assets.loadTexture("island", "./assets/terrain/island.jpg");
    mat->detail = assets.loadTexture("detail", "assets/terrain/detailmap3.jpg");
    mat->detail_scale = 60.f;
    terrain->set_material(mat);

    Camera3D* camera = scene.root().create_child<Camera3D>("fly");
    camera->set_perspective(55.f, 0.1f, 800.f);
    camera->set_position(200.f, terrain->height_at(200.f, 340.f) + 35.f, 460.f);

    scene.set_active_camera(camera);
    scene.ready();

    FlyCam fly;
    fly.speed = 30.f;
    bool showCascades = false;
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
                if (ev.key.keysym.sym == SDLK_c)
                {
                    showCascades = !showCascades;
                    renderer.set_show_cascades(showCascades);
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

        // walk mode: stick the camera to the ground via the height query
        const Uint8* keys = SDL_GetKeyboardState(nullptr);
        if (keys[SDL_SCANCODE_SPACE])
        {
            Vec3 p = camera->get_position();
            camera->set_position(p.x, terrain->height_at(p.x, p.z) + 2.2f, p.z);
        }

        scene.update(dt);
        int w, h;
        app.DrawableSize(&w, &h);
        renderer.render(scene, w, h);
       
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
