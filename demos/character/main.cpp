// SkinnedMeshInstance demo: Sinbad (the classic Ogre character) with GPU
// skinning and Ogre-style layered animation — lower body and upper body play
// different clips at once (IdleBase+IdleTop / RunBase+RunTop), a one-shot
// DrawSwords blends in and out on the top layer, and two swords ride bone
// attachments. Three instances share ONE SkinnedMesh resource.
//
// Controls: WASD/QE + mouse | 1 idle, 2 run, 3 dance | E draw swords |
// F9 stats | F10 gif | ESC

#include "demo_app.hpp"
#include "demo_fly.hpp"
 
#include <scene/Scene.hpp>
#include <scene/SceneRenderer.hpp>
#include <scene/Material.hpp>
#include <scene/SkinnedMesh.hpp>
#include <scene/SkinnedMeshInstance.hpp>
#include <scene/BoneAttachment.hpp>
#include <scene/MeshLoader.hpp>
#include <scene/MeshInstance.hpp>
#include <scene/AssetManager.hpp>

int main(int argc, char** argv)
{
    const int maxFrames = (argc > 1) ? atoi(argv[1]) : 0;

    DemoApp app;
    if (!app.Create("coregl - character")) return 1;
    printf("controls: WASD/QE + mouse | 1 idle 2 run 3 dance | E swords | F9 stats | F10 gif\n");

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
    if (getenv("COREGL_STATS")) renderer.set_show_stats(true);

    Scene scene;
    assets::AssetManager& assets = assets::AssetManager::instance();

    // ── the SHARED resource: one mesh + skeleton + clips for every sinbad ──
    SkinnedMesh sinbadRes;
    if (!sinbadRes.load("assets/models/sinbad/sinbad.h3d"))
    {
        fprintf(stderr, "sinbad load failed\n");
        return 1;
    }
    const char* anims[] = {"IdleBase", "IdleTop",  "RunBase",     "RunTop",
                           "Dance",    "DrawSwords", "HandsRelaxed"};
    for (const char* a : anims)
    {
        char path[128];
        snprintf(path, sizeof(path), "assets/models/sinbad/sinbad_%s.anim", a);
        sinbadRes.load_animations(path);
    }
    // bone list (handy for picking attachment points)
    for (int i = 0; i < sinbadRes.skeleton().bone_count() && getenv("COREGL_BONES"); ++i)
        printf("bone %2d: %s (parent %d)\n", i, sinbadRes.skeleton().bone(i).name.c_str(),
               sinbadRes.skeleton().bone(i).parent);

    Material* bodyMat = scene.create_material();
    bodyMat->diffuse = assets.loadTexture("sinbad_body", "assets/models/sinbad/sinbad_body.tga");
    bodyMat->specular = 0.35f;

    // ground
    Mesh* groundMesh = scene.create_mesh();
    {
        MeshVertex v[4];
        const float h = 30.f;
        const float pos[4][3] = {{-h, 0, -h}, {h, 0, -h}, {h, 0, h}, {-h, 0, h}};
        for (int i = 0; i < 4; ++i)
        {
            v[i].position = Vec3(pos[i][0], pos[i][1], pos[i][2]);
            v[i].normal = Vec3(0.f, 1.f, 0.f);
            v[i].tangent = Vec4(1.f, 0.f, 0.f, 1.f);
            v[i].uv = Vec2((float)(i == 1 || i == 2) * 8.f, (float)(i >= 2) * 8.f);
        }
        const u16 idx[6] = {0, 2, 1, 0, 3, 2};
        groundMesh->set_data(v, 4, idx, 6);
        groundMesh->upload();
    }
    Material* groundMat = scene.create_material(0.55f, 0.55f, 0.5f);
    groundMat->diffuse = assets.loadTexture("ground", "assets/terrain/grass.jpg");
    MeshInstance* ground = scene.root().create_child<MeshInstance>("ground");
    ground->set_mesh(groundMesh);
    ground->set_material(groundMat);

    // ── three characters, one resource — each with its own animation ──
    SkinnedMeshInstance* sinbad[3];
    for (int i = 0; i < 3; ++i)
    {
        sinbad[i] = scene.root().create_child<SkinnedMeshInstance>("sinbad");
        sinbad[i]->set_mesh(&sinbadRes);
        sinbad[i]->set_material(bodyMat);
        sinbad[i]->set_position((float)(i - 1) * 8.f, 5.f, 0.f);
    }
    sinbad[0]->animation().layer(0).play("IdleBase");
    sinbad[0]->animation().layer(1).play("IdleTop");
    sinbad[1]->animation().layer(0).play("RunBase");
    sinbad[1]->animation().layer(1).play("RunTop");
    sinbad[2]->animation().layer(0).play("Dance");

    // ── sword on the right hand of the middle sinbad (bone attachment) ──
    Mesh swordMesh;
    Material* swordMat = scene.create_material();
    swordMat->diffuse = assets.loadTexture("sword", "assets/models/sinbad/sinbad_sword.tga");
    if (MeshLoader::load("assets/models/sinbad/sword.h3d", swordMesh))
    {
        swordMesh.upload();
        BoneAttachment* hand = sinbad[1]->create_child<BoneAttachment>("hand");
        if (!hand->attach(sinbad[1], "Handle.R")) hand->attach(sinbad[1], "Hand.R");
        MeshInstance* sword = hand->create_child<MeshInstance>("sword");
        sword->set_mesh(&swordMesh);
        sword->set_material(swordMat);
    }

    Camera3D* camera = scene.root().create_child<Camera3D>("fly");
    camera->set_perspective(50.f, 0.1f, 300.f);
    camera->set_position(0.f, 7.f, 22.f);

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
                AnimationPlayer& anim = sinbad[1]->animation();
                if (ev.key.keysym.sym == SDLK_ESCAPE) running = false;
                if (ev.key.keysym.sym == SDLK_1)
                {
                    anim.layer(0).cross_fade("IdleBase", 0.3f);
                    anim.layer(1).cross_fade("IdleTop", 0.3f);
                }
                if (ev.key.keysym.sym == SDLK_2)
                {
                    anim.layer(0).cross_fade("RunBase", 0.3f);
                    anim.layer(1).cross_fade("RunTop", 0.3f);
                }
                if (ev.key.keysym.sym == SDLK_3)
                {
                    anim.layer(0).cross_fade("Dance", 0.3f);
                    anim.layer(1).stop();
                }
                if (ev.key.keysym.sym == SDLK_e)
                    anim.layer(1).play_one_shot("DrawSwords", "IdleTop", 0.2f);
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
        scene.update(dt);

        int w, h;
        app.DrawableSize(&w, &h);
        renderer.render(scene, w, h);
 
        app.EndFrame();

        ++frame;
        if (maxFrames > 0 && frame >= maxFrames) running = false;
    }

    printf("frames: %d | bones: %d | clips: %u\n", frame, sinbadRes.skeleton().bone_count(),
           (gl::u32)sinbadRes.clips().size());
    sinbadRes.release_gpu();
    swordMesh.release_gpu();
    scene.release_gpu();
    assets.release();
    renderer.release();
    app.Destroy();
    return 0;
}
