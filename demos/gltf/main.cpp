// glTF skinned-mesh demo: raylib's "greenman" sample, loaded through
// AssetManager::loadSkinnedMesh()/loadAnimation() — resource ownership
// stays with AssetManager (assets.release() frees it, no manual
// release_gpu() in the demo), same as every other asset type. Proves the
// glTF dispatch inside SkinnedMesh::load()/load_animations() is a true
// drop-in for the native .mesh/.anim format: only the file paths differ,
// nothing about the loading call sites changes. idle.glb is a separate
// animation-only glTF (same skeleton, no mesh) loaded via loadAnimation(),
// mirroring how a native .anim file works.
//
// Controls: WASD/QE + mouse | F10 gif | ESC

#include "demo_app.hpp"
#include "demo_fly.hpp"
#include <scene/AssetManager.hpp>
#include <scene/Material.hpp>
#include <scene/Scene.hpp>
#include <scene/SceneRenderer.hpp>
#include <scene/SkinnedMesh.hpp>
#include <scene/SkinnedMeshInstance.hpp>

int main(int argc, char** argv)
{
    const int maxFrames = (argc > 1) ? atoi(argv[1]) : 0;

    DemoApp app;
    if (!app.Create("coregl - gltf skinned mesh")) return 1;
    printf("controls: WASD/QE + mouse | F10 gif | ESC\n");

    SceneRenderer renderer;
    if (!renderer.init())
    {
        fprintf(stderr, "renderer init failed\n");
        app.Destroy();
        return 1;
    }
    renderer.set_light_dir(Vec3(0.35f, -0.75f, 0.45f));
    renderer.set_sky_enabled(true);
    renderer.enable_shadows();

    Scene scene;
    assets::AssetManager& assets = assets::AssetManager::instance();

    SkinnedMesh* greenmanRes = assets.loadSkinnedMesh("greenman", "assets/models/gltf/robot.glb");
    if (!greenmanRes)
    {
        fprintf(stderr, "greenman.glb load failed\n");
        return 1;
    }
 

    for (int i = 0; i < greenmanRes->skeleton().bone_count() && getenv("COREGL_BONES"); ++i)
        printf("bone %2d: %s (parent %d)\n", i, greenmanRes->skeleton().bone(i).name.c_str(),
               greenmanRes->skeleton().bone(i).parent);

    SkinnedMeshInstance* greenman = scene.root().create_child<SkinnedMeshInstance>("greenman");
    greenman->set_mesh(greenmanRes);
    greenman->set_position(0.f, 0.f, 0.f);
    if (greenmanRes->find_clip("1_idle"))
        greenman->animation().layer(0).play("1_idle");
    else if (!greenmanRes->clips().empty())
        greenman->animation().layer(0).play(greenmanRes->clips()[0]->name());

    Camera3D* camera = scene.root().create_child<Camera3D>("fly");
    camera->set_perspective(50.f, 0.1f, 300.f);
    camera->set_position(0.f, 1.6f, 4.f);

    scene.set_active_camera(camera);
    scene.ready();

    FlyCam fly;
    fly.speed = 3.f;
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
        scene.update(dt);

        int w, h;
        app.DrawableSize(&w, &h);
        renderer.render(scene, w, h);
        app.EndFrame();

        ++frame;
        if (maxFrames > 0 && frame >= maxFrames) running = false;
    }

    if (getenv("COREGL_GIF_AUTOSTART")) app.gif.Toggle(0, 0);
    printf("frames: %d | bones: %d | clips: %u\n", frame, greenmanRes->skeleton().bone_count(),
           (gl::u32)greenmanRes->clips().size());
    scene.release_gpu();
    assets.release();
    renderer.release();
    app.Destroy();
    return 0;
}
