// BillboardNode demo: a row of single-quad billboards, one per view mode
// (Free/Upright/Fixed), plus an atlas-rect test — same texture (fire.png,
// a 4x2 flame sprite sheet) sampled at different cells to prove
// set_atlas_grid() picks the right one. Fly around to see Free track the
// camera on every axis, Upright stay vertical, and Fixed never turn.
//
// Controls: WASD/QE + mouse, LSHIFT fast, F10 gif, ESC quit.

#include "demo_app.hpp"
#include "demo_fly.hpp"
#include <scene/AssetManager.hpp>
#include <scene/BillboardNode.hpp>
#include <scene/Material.hpp>
#include <scene/MeshInstance.hpp>
#include <scene/Scene.hpp>
#include <scene/SceneRenderer.hpp>

int main(int argc, char** argv)
{
    const int maxFrames = (argc > 1) ? atoi(argv[1]) : 0;

    DemoApp app;
    if (!app.Create("coregl - billboard test")) return 1;

    SceneRenderer renderer;
    if (!renderer.init())
    {
        fprintf(stderr, "renderer init failed\n");
        app.Destroy();
        return 1;
    }
    renderer.set_light_dir(Vec3(0.4f, -0.7f, 0.3f));
    renderer.set_sky_enabled(true);

    Scene scene;
    assets::AssetManager& assets = assets::AssetManager::instance();

    Mesh* ground = assets.createPlane("ground", 40.f, 40.f, 4.f);
    Material* groundMat = scene.create_material(0.3f, 0.35f, 0.3f);
    MeshInstance* groundInst = scene.root().create_child<MeshInstance>("ground");
    groundInst->set_mesh(ground);
    groundInst->set_material(groundMat);

    gl::Texture* fireTex = assets.loadTexture("fire", "assets/textures/fire.png");
    gl::Texture* boomTex = assets.loadTexture("boom", "assets/textures/boom.jpg");

    // fire.png is an 8x8 flip-book (64 frames), not static variants — three
    // billboards, one per view mode, all animating the same loop
    BillboardNode* freeB = scene.root().create_child<BillboardNode>("free");
    freeB->set_position(-4.f, 2.f, 0.f);
    freeB->set_size(2.f, 2.f);
    freeB->set_view_mode(BillboardViewMode::Free);
    freeB->set_animated_grid(8, 8, 12.f);
    freeB->texture = fireTex;

    BillboardNode* uprightB = scene.root().create_child<BillboardNode>("upright");
    uprightB->set_position(0.f, 2.f, 0.f);
    uprightB->set_size(2.f, 2.f);
    uprightB->set_view_mode(BillboardViewMode::Upright);
    uprightB->set_animated_grid(8, 8, 12.f);
    uprightB->texture = fireTex;

    BillboardNode* fixedB = scene.root().create_child<BillboardNode>("fixed");
    fixedB->set_position(4.f, 2.f, 0.f);
    fixedB->set_size(2.f, 2.f);
    fixedB->set_view_mode(BillboardViewMode::Fixed);
    fixedB->set_rotation(Quaternion::FromAxisAngle(Vec3(0.f, 1.f, 0.f), 0.6f));
    fixedB->set_animated_grid(8, 8, 12.f);
    fixedB->texture = fireTex;

    // static-frame test: four billboards each pinned to a different fixed
    // cell (not animating) — proves set_atlas_grid() addresses the right
    // sub-rect independently of the animated ones above
    BillboardNode* atlasB[4];
    for (int i = 0; i < 4; ++i)
    {
        char name[16];
        snprintf(name, sizeof(name), "atlas_%d", i);
        atlasB[i] = scene.root().create_child<BillboardNode>(name);
        atlasB[i]->set_position(-6.f + (float)i * 2.f, 2.f, -6.f);
        atlasB[i]->set_size(1.6f, 1.6f);
        atlasB[i]->set_view_mode(BillboardViewMode::Upright);
        atlasB[i]->set_atlas_grid(8, 8, i * 2, i);
        atlasB[i]->texture = fireTex;
    }

    // boom.jpg: a cleaner 4x4 (16-frame) explosion sheet — easier to read
    // frame-by-frame than the dense 8x8 fire, good for spotting any real
    // stutter/skip in the animation logic
    BillboardNode* boomB = scene.root().create_child<BillboardNode>("boom");
    boomB->set_position(0.f, 2.f, -3.f);
    boomB->set_size(2.5f, 2.5f);
    boomB->set_view_mode(BillboardViewMode::Upright);
    boomB->set_animated_grid(4, 4, 10.f);
    boomB->texture = boomTex;

    Camera3D* camera = scene.root().create_child<Camera3D>("fly");
    camera->set_perspective(55.f, 0.1f, 200.f);
    camera->set_position(0.f, 4.f, 12.f);

    scene.set_active_camera(camera);
    scene.ready();

    FlyCam fly;
    fly.speed = 10.f;
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

    printf("frames: %d\n", frame);
    scene.release_gpu();
    assets.release();
    renderer.release();
    app.Destroy();
    return 0;
}
