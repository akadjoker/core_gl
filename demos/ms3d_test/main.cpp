// MilkShape3D (.ms3d) loader demo — verifies scene/MS3DLoader.cpp end to
// end: a static mesh via AssetManager::load_ms3d_mesh (crate.ms3d) side by
// side with a skeletal, keyframe-animated one via SkinnedMesh (ninja.ms3d,
// the classic MS3D SDK tutorial model) — the real proof the joint
// hierarchy + keyframe conversion (MS3D's Euler-deltas-on-bind convention,
// resampled onto AnimationClip's shared-time-axis BoneTrack) actually
// deforms correctly, not just "loads without crashing".
//
// Controls: WASD/QE + mouse, LSHIFT fast, B mesh-bounds debug, Z wireframe,
// F10 gif, ESC quit.

#include "demo_app.hpp"
#include "demo_fly.hpp"
#include <scene/AssetManager.hpp>
#include <scene/Material.hpp>
#include <scene/MeshInstance.hpp>
#include <scene/Scene.hpp>
#include <scene/SceneRenderer.hpp>
#include <scene/SkinnedMesh.hpp>
#include <scene/SkinnedMeshInstance.hpp>

int main(int argc, char** argv)
{
    const int maxFrames = (argc > 1) ? atoi(argv[1]) : 0;

    DemoApp app;
    if (!app.Create("coregl - ms3d test")) return 1;

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

    Mesh* ground = assets.createPlane("ground", 40.f, 40.f, 4.f);
    Material* groundMat = scene.create_material(0.4f, 0.4f, 0.4f);
    MeshInstance* groundInst = scene.root().create_child<MeshInstance>("ground");
    groundInst->set_mesh(ground);
    groundInst->set_material(groundMat);

    // ── static path: crate.ms3d, geometry only ──
    std::vector<Material*> crateMats;
    Mesh* crateMesh = assets.load_ms3d_mesh("crate", "assets/models/ms3d/level.ms3d", crateMats);
    if (crateMesh)
    {
        MeshInstance* crate = scene.root().create_child<MeshInstance>("crate");
        crate->set_mesh(crateMesh);
        crate->set_scale(0.01f); 
        crate->set_materials(crateMats);
    }
    else
    {
        fprintf(stderr, "crate.ms3d load failed\n");
    }

    // ── skinned path: ninja.ms3d, joints + its own embedded keyframes ──
    SkinnedMesh* ninjaRes = assets.loadSkinnedMesh("ninja", "assets/models/ms3d/ninja.ms3d");
    SkinnedMeshInstance* ninja = nullptr;
    if (ninjaRes)
    {
        // ninja.ms3d carries both the mesh and its own animation in one
        // file — load_animations() on the same path pulls the joints'
        // keyframes in as a clip (see SkinnedMesh::load_animations_ms3d)
        assets.loadAnimation("ninja", "assets/models/ms3d/ninja.ms3d");

        ninja = scene.root().create_child<SkinnedMeshInstance>("ninja");
        ninja->set_mesh(ninjaRes);
        ninja->set_position(4.f, 0.f, 0.f);
      //  ninja->set_scale(0.05f); // MS3D SDK tutorial models are authored at a large scale

        if (!ninjaRes->clips().empty())
        {
            const std::string& clipName = ninjaRes->clips()[0]->name();
            ninja->animation().layer(0).play(clipName);
            printf("ninja: playing clip '%s' (%.2fs, %d bones)\n", clipName.c_str(),
                  ninjaRes->clips()[0]->duration(), ninjaRes->skeleton().bone_count());
        }
        else
        {
            fprintf(stderr, "ninja.ms3d: mesh loaded but no animation clip found\n");
        }
    }
    else
    {
        fprintf(stderr, "ninja.ms3d load failed\n");
    }

    Camera3D* camera = scene.root().create_child<Camera3D>("fly");
    camera->set_perspective(55.f, 0.1f, 200.f);
    camera->set_position(0.f, 6.f, 14.f);

    scene.set_active_camera(camera);
    scene.ready();

    FlyCam fly;
    fly.speed = 10.f;
    gl::u64 lastTicks = SDL_GetPerformanceCounter();
    const gl::u64 freq = SDL_GetPerformanceFrequency();

    bool showBounds = false;
    bool wireframe = false;

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
                if (ev.key.keysym.sym == SDLK_b)
                {
                    showBounds = !showBounds;
                    renderer.set_show_mesh_bounds(showBounds);
                    printf("mesh bounds: %s\n", showBounds ? "on" : "off");
                }
                if (ev.key.keysym.sym == SDLK_z)
                {
                    wireframe = !wireframe;
                    renderer.set_wireframe(wireframe);
                    printf("wireframe: %s\n", wireframe ? "on" : "off");
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
    assets.release();
    renderer.release();
    app.Destroy();
    return 0;
}
