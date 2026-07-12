// BSP map demo: loads a Quake 3 BSP (IBSP v46) through the AssetManager,
// renders it with CSM shadows, sky, and a fly camera. The BSP loader
// tessellates Bezier patches and creates one Material per texture group.
//
// Controls: WASD/QE + mouse, LSHIFT fast, F9 stats, P print camera,
//           T/G time-of-day, F10 gif, ESC quit.

#include "demo_app.hpp"
#include "demo_fly.hpp"
#include <scene/Scene.hpp>
#include <scene/SceneRenderer.hpp>
#include <scene/Material.hpp>
#include <scene/MeshInstance.hpp>
#include <scene/BspInstance.hpp>
#include <scene/LightNode.hpp>
#include <scene/Mesh.hpp>
#include <scene/AssetManager.hpp>
#include <scene/Filesystem.hpp>
#include <chrono>
#include <vector>

int main(int argc, char** argv)
{
    const int maxFrames = (argc > 1) ? atoi(argv[1]) : 0;

    DemoApp app;
    if (!app.Create("coregl - bsp")) return 1;
    printf("controls: WASD/QE + mouse | LSHIFT fast | F9 stats | P print camera | T/G time-of-day | F10 gif\n");

    SceneRenderer renderer;
    if (!renderer.init())
    {
        fprintf(stderr, "renderer init failed\n");
        app.Destroy();
        return 1;
    }
    renderer.set_light_dir(Vec3(0.4f, -0.8f, 0.3f));
    renderer.set_sky_enabled(true);
    renderer.enable_shadows(2, 1024, 200.f);
    if (getenv("COREGL_STATS")) renderer.set_show_stats(true);
    if (getenv("COREGL_CASCADES")) renderer.set_show_cascades(true);

    Scene scene;
    assets::AssetManager& assets = assets::AssetManager::instance();

    // ---- filesystem: register asset paths so textures resolve automatically ---
    fs::getFilesystem().addFolder("assets/bsp/oa_rpg3dm2");

    // ---- load BSP map -------------------------------------------------------
    auto t0 = std::chrono::steady_clock::now();
    std::vector<Material*> materials;
    // textureDir is the map root — BSP texture names like
    // "textures/base_floor/clang" are cleaned and joined: root + "textures/..." + ext
    Mesh* bspMesh = assets.load_bsp_mesh("bsp_map",
                                          "assets/bsp/oa_rpg3dm2/oa_rpg3dm2.bsp",
                                          materials,
                                          "assets/bsp/oa_rpg3dm2/");
    auto t1 = std::chrono::steady_clock::now();

    if (!bspMesh)
    {
        fprintf(stderr, "BSP load failed (run from repo root)\n");
        app.Destroy();
        return 1;
    }

    const double loadMs = std::chrono::duration<double, std::milli>(t1 - t0).count();
    printf("bsp: %u surfaces, %u verts, %u indices, %u materials | load %.1fms\n",
           (unsigned)bspMesh->surfaces().size(), bspMesh->vertex_count(),
           bspMesh->index_count(), (unsigned)materials.size(), loadMs);

    // ---- scene graph --------------------------------------------------------
    BspInstance* mapInst = scene.root().create_child<BspInstance>("bsp_map");
    mapInst->set_mesh(bspMesh);
    mapInst->set_materials(materials);
    // Q3 maps are in game units (~32 units = 1m); scale down for a
    // reasonable camera speed. Adjust this to taste for each map.
    mapInst->set_scale(0.03125f); // 1/32
 
    // headlamp: point light following the camera for dark corners
    PointLight* headlamp = scene.root().create_child<PointLight>("headlamp");
    headlamp->color = Vec3(1.f, 1.f, 0.95f);
    headlamp->intensity = 200.0f;
    headlamp->range = 5000.f;
    headlamp->cast_shadows = false;

    Camera3D* camera = scene.root().create_child<Camera3D>("fly");
    camera->set_perspective(60.f, 0.1f, 500.f);
    camera->set_position(0.f, 3.f, 0.f);

    scene.set_active_camera(camera);
    scene.ready();

    FlyCam fly;
    fly.speed = 12.f;
    gl::u64 lastTicks = SDL_GetPerformanceCounter();
    const gl::u64 freq = SDL_GetPerformanceFrequency();
    float timeOfDay = getenv("COREGL_TOD") ? (float)atof(getenv("COREGL_TOD")) : 0.9f;

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
                if (ev.key.keysym.sym == SDLK_x)
                {
                    static bool on = true;
                    on = !on;
                    renderer.set_shadows_active(on);
                    printf("shadows: %s\n", on ? "ON" : "off");
                }
                if (ev.key.keysym.sym == SDLK_c)
                {
                    static bool on = false;
                    on = !on;
                    renderer.set_show_cascades(on);
                    printf("cascade debug colors: %s\n", on ? "ON" : "off");
                }
                if (ev.key.keysym.sym == SDLK_p)
                {
                    Vec3 p = camera->get_position();
                    printf("camera pos (%.2f, %.2f, %.2f) yaw=%.3f pitch=%.3f\n",
                           p.x, p.y, p.z, fly.yaw, fly.pitch);
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

        // time-of-day: T/G move the sun
        const Uint8* keys = SDL_GetKeyboardState(nullptr);
        if (keys[SDL_SCANCODE_T]) timeOfDay += dt * 0.4f;
        if (keys[SDL_SCANCODE_G]) timeOfDay -= dt * 0.4f;
        Vec3 sunDir = Vec3(cosf(timeOfDay) * 0.8f, sinf(timeOfDay), 0.35f).normalized();
        renderer.set_light_dir(sunDir * -1.f);

        fly.apply(camera, dt);
        headlamp->set_position(camera->get_position());

        scene.update(dt);

        int w, h;
        app.DrawableSize(&w, &h);
        renderer.render(scene, w, h);
        app.EndFrame();

        ++frame;
        if (maxFrames > 0 && frame >= maxFrames) running = false;
    }

    printf("frames: %d\n", frame);

    // materials are owned by this demo (BSP loader allocated them)
    for (Material* m : materials) delete m;

    scene.release_gpu();
    assets.release(); // frees the BSP mesh + textures
    renderer.release();
    app.Destroy();
    return 0;
}
