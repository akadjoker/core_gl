// Powerplant demo: same stress-test structure as demo_sponza, pointed at a
// much bigger, open static mesh (65 surfaces, 12MB .h3d) — checks the
// forward pass + shadows at open-world scale instead of a tight interior.
//
// Controls: WASD/QE + mouse, LSHIFT fast, F9 stats, P print camera,
// X toggle shadows, C cascade colors, L light gizmos, F10 gif, ESC quit.

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
    if (!app.Create("coregl - powerplant")) return 1;
    printf("controls: WASD/QE + mouse | LSHIFT fast | F9 stats | P print camera | X toggle "
           "shadows | C cascade colors | L light gizmos | F10 gif\n");

    SceneRenderer renderer;
    if (!renderer.init())
    {
        fprintf(stderr, "renderer init failed\n");
        app.Destroy();
        return 1;
    }
    renderer.set_light_dir(Vec3(0.4f, -0.8f, 0.3f));
    renderer.set_sky_enabled(true);
    if (getenv("COREGL_STATS")) renderer.set_show_stats(true);
    if (getenv("COREGL_CASCADES")) renderer.set_show_cascades(true);
    if (getenv("COREGL_GIZMOS")) renderer.set_show_light_gizmos(true);

    Scene scene;
    assets::AssetManager& assets = assets::AssetManager::instance();

    auto t0 = std::chrono::steady_clock::now();
    Mesh plantMesh;
    std::vector<MeshLoader::MaterialDesc> matDescs;
    if (!MeshLoader::load("assets/powerplant/powerplant.h3d", plantMesh, &matDescs))
    {
        fprintf(stderr, "powerplant load failed\n");
        return 1;
    }
    auto t1 = std::chrono::steady_clock::now();
    plantMesh.upload();
    auto t2 = std::chrono::steady_clock::now();

    const double parseMs = std::chrono::duration<double, std::milli>(t1 - t0).count();
    const double uploadMs = std::chrono::duration<double, std::milli>(t2 - t1).count();
    printf("powerplant: %u surfaces, %u verts, %u indices | parse %.1fms, upload %.1fms\n",
           (unsigned)plantMesh.surfaces().size(), plantMesh.vertex_count(),
           plantMesh.index_count(), parseMs, uploadMs);

    // real bounding box from the surfaces (no guessed scale/camera constants
    // — sponza needed a 0.01 fudge factor because it's authored in cm; this
    // mesh's actual unit scale is unknown up front, so measure it instead)
    Vec3 bmin(1e30f, 1e30f, 1e30f), bmax(-1e30f, -1e30f, -1e30f);
    for (const Surface& s : plantMesh.surfaces())
    {
        bmin = bmin.Min(s.bounds.min);
        bmax = bmax.Max(s.bounds.max);
    }
    const Vec3 center = (bmin + bmax) * 0.5f;
    const Vec3 size = bmax - bmin;
    const float extent = std::max(size.x, std::max(size.y, size.z));
    printf("powerplant bounds: min(%.1f %.1f %.1f) max(%.1f %.1f %.1f) extent=%.1f\n", bmin.x,
           bmin.y, bmin.z, bmax.x, bmax.y, bmax.z, extent);

    // shadow distance/resolution tuned to the scene's own scale. Distance
    // must cover the whole extent (the 200 default clipped casters near the
    // edge of this ~245-unit scene, popping their shadows in/out); bumping
    // resolution to 4096 keeps roughly the same texels-per-world-unit
    // density Sponza had at 2048/200, so edges don't get blockier just
    // because the covered distance grew.
    renderer.enable_shadows(4, 1024, extent * 1.3f);

    std::vector<Material*> materials;
    materials.reserve(matDescs.size());
    for (const MeshLoader::MaterialDesc& md : matDescs)
    {
        Material* mat = scene.create_material(md.diffuse.x, md.diffuse.y, md.diffuse.z);
        mat->specular = (md.specular.x + md.specular.y + md.specular.z) / 3.f;
        mat->shininess = md.shininess;
        if (!md.textures.empty())
        {
            // the .h3d material table still names the source .dds files;
            // the folder only has the converted .png versions
            std::string name = md.textures[0];
            size_t dot = name.find_last_of('.');
            if (dot != std::string::npos) name = name.substr(0, dot) + ".png";
            std::string path = "assets/powerplant/textures/" + name;
            mat->diffuse = assets.loadTexture(name.c_str(), path.c_str());
        }
        materials.push_back(mat);
    }

    MeshInstance* plant = scene.root().create_child<MeshInstance>("powerplant");
    plant->set_mesh(&plantMesh);
    plant->set_materials(materials);

    // 3 static point lights spread over the structure (positions scaled
    // from the measured extent, not guessed) + 1 headlamp on the camera —
    // 4 total, the engine's per-frame point-light budget
    const Vec3 lightPos[] = {
        center + Vec3(-extent * 0.2f, extent * 0.15f, -extent * 0.2f),
        center + Vec3(extent * 0.2f, extent * 0.15f, extent * 0.1f),
        center + Vec3(0.f, extent * 0.25f, extent * 0.25f),
    };
        PointLight* light = scene.root().create_child<PointLight>("plant_light");
        const Vec3& p = lightPos[1];
        light->set_position(p);
        light->color = Vec3(1.0f, 0.4f, 0.5f);
        light->intensity = 100.5f;
        light->range = extent * 0.3f;
        light->cast_shadows = true;
   
    const Vec3 orbitCenter = lightPos[0];
    const float orbitRadius = extent * 0.15f;

    Camera3D* camera = scene.root().create_child<Camera3D>("fly");
    camera->set_perspective(60.f, extent * 0.001f, extent * 4.f);
    camera->set_position(-68.92, 20.31, 31.51);

    PointLight* headlamp = scene.root().create_child<PointLight>("headlamp");
    headlamp->color = Vec3(1.f, 1.f, 0.95f);
    headlamp->intensity = 20.5f;
    headlamp->range = extent * 0.35f;
    headlamp->cast_shadows = true; // moves with the camera every frame — same reason as orbitLight

    scene.set_active_camera(camera);
    scene.ready();

    FlyCam fly;
    fly.speed = extent * 0.08f;
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
                    printf("camera pos (%.2f, %.2f, %.2f) yaw=%.3f pitch=%.3f\n", p.x, p.y, p.z,
                           fly.yaw, fly.pitch);
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

        fly.apply(camera, dt);

        // one light flies a horizontal circle + bob around its start point
        static float orbitTime = 0.f;
        if (light)
        {
            orbitTime += dt * 0.8f;
            light->set_position(orbitCenter.x + cosf(orbitTime) * orbitRadius,
                                     orbitCenter.y + sinf(orbitTime * 1.7f) * orbitRadius * 0.4f,
                                     orbitCenter.z + sinf(orbitTime) * orbitRadius);
        }
        headlamp->set_position(camera->get_position());

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
    plantMesh.release_gpu();
    scene.release_gpu();
    assets.release();
    renderer.release();
    app.Destroy();
    return 0;
}
