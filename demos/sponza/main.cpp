// Sponza demo: a real, heavy static mesh (393 surfaces, 8.3MB .h3d) to
// stress-test the loader and forward pass at scale — the classic scene for
// checking whether a renderer holds up on real content, not toy geometry.
//
// Controls: WASD/QE + mouse, LSHIFT fast, F9 stats, F10 gif, ESC quit.

#include "demo_app.hpp"
#include "demo_fly.hpp"
#include "demo_perf.hpp"
#include <scene/Scene.hpp>
#include <scene/SceneRenderer.hpp>
#include <scene/Material.hpp>
#include <scene/MeshInstance.hpp>
#include <scene/MeshLoader.hpp>
#include <scene/LightNode.hpp>
#include <scene/Mesh.hpp>
#include <scene/AssetManager.hpp>
#include <chrono>

int main(int argc, char** argv)
{
    const int maxFrames = (argc > 1) ? atoi(argv[1]) : 0;

    DemoApp app;
    if (!app.Create("coregl - sponza")) return 1;
    printf("controls: WASD/QE + mouse | LSHIFT fast | F9 stats | P print camera | X toggle shadows | C cascade colors | L light gizmos | F10 gif\n");

    SceneRenderer renderer;
    if (!renderer.init())
    {
        fprintf(stderr, "renderer init failed\n");
        app.Destroy();
        return 1;
    }
    renderer.set_light_dir(Vec3(0.4f, -0.8f, 0.3f));
    renderer.set_sky_enabled(true);
    // Scene is ~10 units across (sponza at scale 0.01). The default
    // shadow_distance=200 with 4 cascades (caster padding 80-250!) keeps
    // every surface in every cascade every frame. 2 cascades + distance=30
    // is plenty for a small indoor scene and cuts shadow draw calls ~4×.
    renderer.enable_shadows(2, 1024, 30.f);
    if (getenv("COREGL_STATS")) renderer.set_show_stats(true);
    if (getenv("COREGL_CASCADES")) renderer.set_show_cascades(true);
    if (getenv("COREGL_GIZMOS")) renderer.set_show_light_gizmos(true);

    Scene scene;
    assets::AssetManager& assets = assets::AssetManager::instance();

    // load timing: parse + upload, separately, so a slow run tells us which
    // half to optimize
    auto t0 = std::chrono::steady_clock::now();
    Mesh sponzaMesh;
    std::vector<MeshLoader::MaterialDesc> matDescs;
    if (!MeshLoader::load("assets/sponza/sponza.h3d", sponzaMesh, &matDescs))
    {
        fprintf(stderr, "sponza load failed\n");
        return 1;
    }
    auto t1 = std::chrono::steady_clock::now();
    sponzaMesh.upload();
    auto t2 = std::chrono::steady_clock::now();

    const double parseMs =
        std::chrono::duration<double, std::milli>(t1 - t0).count();
    const double uploadMs =
        std::chrono::duration<double, std::milli>(t2 - t1).count();
    printf("sponza: %u surfaces, %u verts, %u indices | parse %.1fms, upload %.1fms\n",
           (unsigned)sponzaMesh.surfaces().size(), sponzaMesh.vertex_count(),
           sponzaMesh.index_count(), parseMs, uploadMs);

    // one Material per material slot, textures resolved through the
    // AssetManager (dedup + checkerboard fallback for anything missing)
    std::vector<Material*> materials;
    materials.reserve(matDescs.size());
    for (const MeshLoader::MaterialDesc& md : matDescs)
    {
        Material* mat = scene.create_material(md.diffuse.x, md.diffuse.y, md.diffuse.z);
        mat->specular = (md.specular.x + md.specular.y + md.specular.z) / 3.f;
        mat->shininess = md.shininess;
        if (!md.textures.empty())
        {
            std::string path = "assets/sponza/textures/" + md.textures[0];
            mat->diffuse = assets.loadTexture(md.textures[0].c_str(), path.c_str());
        }
        materials.push_back(mat);
    }

    MeshInstance* sponza = scene.root().create_child<MeshInstance>("sponza");
    sponza->set_mesh(&sponzaMesh);
    sponza->set_materials(materials);
    sponza->set_scale(0.01f); // sponza.h3d is authored in cm-ish units

    // torch-style point lights along the corridor, near the potted plants
    // seen at these vantage points (warm color, modest range)
    const Vec3 torchPos[] = {
        Vec3(1.9f, 1.3f, -1.2f),  Vec3(-1.9f, 1.3f, -1.2f),
        Vec3(1.9f, 1.3f, -4.5f),  Vec3(-1.9f, 1.3f, -4.5f),
    };
    PointLight* orbitLight = nullptr;
    for (size_t i = 0; i < 3; ++i)
    {
        PointLight* torch = scene.root().create_child<PointLight>("torch");
        torch->set_position(torchPos[i]);
        torch->color = Vec3(1.0f, 0.65f, 0.3f);
        torch->intensity = 1.4f;
        torch->range = 8.5f;
        torch->cast_shadows = (i < 1); // only 1 torch shadows: 2 point-light shadow slots total
        if (i == 0) orbitLight = torch; // this one flies a circle + bob, see the update loop
    }
    const Vec3 orbitCenter = torchPos[0];

    Camera3D* camera = scene.root().create_child<Camera3D>("fly");
    camera->set_perspective(60.f, 0.05f, 100.f);
    camera->set_position(0.f, 3.f, 0.f);

    // headlamp: point light glued to the camera, so wherever you look is lit
    PointLight* headlamp = scene.root().create_child<PointLight>("headlamp");
    headlamp->color = Vec3(1.f, 1.f, 0.95f);
    headlamp->intensity = 2.2f;
    headlamp->range = 42.f;
    // headlamp follows the camera — casting shadows means 6 cube faces
    // re-render the whole scene every frame. Disable it; the CSM sun
    // shadows are enough for this scene.
    headlamp->cast_shadows = false;

    scene.set_active_camera(camera);
    scene.ready();

    FlyCam fly;
    fly.speed = 6.f;
    gl::u64 lastTicks = SDL_GetPerformanceCounter();
    const gl::u64 freq = SDL_GetPerformanceFrequency();
  float timeOfDay = getenv("COREGL_TOD") ? (float)atof(getenv("COREGL_TOD")) : 0.9f;

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
                if (ev.key.keysym.sym == SDLK_l)
                {
                    static bool on = false;
                    on = !on;
                    renderer.set_show_light_gizmos(on);
                    printf("light gizmos: %s\n", on ? "ON" : "off");
                }
                if (ev.key.keysym.sym == SDLK_p)
                {
                    Vec3 p = camera->get_position();
                    printf("camera pos (%.2f, %.2f, %.2f) yaw=%.3f pitch=%.3f\n", p.x, p.y,
                           p.z, fly.yaw, fly.pitch);
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


     const Uint8* keys = SDL_GetKeyboardState(nullptr);
        if (keys[SDL_SCANCODE_T]) timeOfDay += dt * 0.4f;
        if (keys[SDL_SCANCODE_G]) timeOfDay -= dt * 0.4f;
        Vec3 sunDir = Vec3(cosf(timeOfDay) * 0.8f, sinf(timeOfDay), 0.35f).normalized();
        renderer.set_light_dir(sunDir * -1.f);


        // automated rotation-only test: same position, sweeping yaw, to
        // isolate the shadow-swim bug from camera translation
        if (getenv("COREGL_ROTTEST"))
        {
            camera->set_position(1.89f, 1.06f, -1.19f);
            // sweep pitch upward like the user's 3 screenshots, fixed yaw/pos
            camera->set_euler(Vec3(-0.715f + (float)frame * 0.05f, -1.010f, 0.f));
        }
        else
            fly.apply(camera, dt);

        // one torch flies a horizontal circle around its start position
        // while bobbing up/down — proves point-light shading updates live,
        // not just at scene-build time (specular highlights sweep with it)
        static float orbitTime = 0.f;
        if (orbitLight)
        {
            orbitTime += dt * 1.2f;
             orbitLight->set_position(orbitCenter.x + cosf(orbitTime) * 4.0f,
                                      orbitCenter.y + sinf(orbitTime * 9.7f) * 0.6f,
                                      orbitCenter.z + sinf(orbitTime) * 1.0f);
                        // headlamp->set_position(orbitCenter.x + cosf(orbitTime) * 4.0f,
                        //              orbitCenter.y + sinf(orbitTime * 9.7f) * 0.6f,
                        //              orbitCenter.z + sinf(orbitTime) * 1.0f);

        }

        headlamp->set_position(camera->get_position());
//        orbitLight->set_position(camera->get_position());
        scene.update(dt);

        int w, h;
        app.DrawableSize(&w, &h);
        renderer.render(scene, w, h);
        perf.tick(frame, renderer.last_item_count(), dt);
        app.EndFrame();

        ++frame;
        if (maxFrames > 0 && frame >= maxFrames) running = false;
    }

    printf("frames: %d\n", frame);
    sponzaMesh.release_gpu();
    scene.release_gpu();
    assets.release();
    renderer.release();
    app.Destroy();
    return 0;
}
