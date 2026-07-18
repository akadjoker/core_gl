// Mesh-vs-mesh collision demo: two cubes on separate orbits sweep through
// each other periodically. Verifies scene/MeshCollision.hpp end to end —
// box-tree broad phase + triangle-triangle narrow phase, real contact
// point/normal/depth, via the CollisionRegistry add/remove API (not the
// sphere-sphere approximation games/hoverjet uses today).
//
// Visual proof: both cubes flash red while overlapping (green otherwise),
// and a small yellow marker sphere sits at the reported contact point,
// oriented along the reported normal (a thin stretched cylinder would show
// the normal better, but a marker position at least proves the point is on
// the actual overlap, not just "somewhere between the two centers").
//
// Controls: WASD/QE + mouse, LSHIFT fast, F10 gif, ESC quit.

#include "demo_app.hpp"
#include "demo_fly.hpp"
#include <scene/AssetManager.hpp>
#include <scene/Material.hpp>
#include <scene/MeshCollision.hpp>
#include <scene/MeshInstance.hpp>
#include <scene/Scene.hpp>
#include <scene/SceneRenderer.hpp>
#include <cmath>

int main(int argc, char** argv)
{
    const int maxFrames = (argc > 1) ? atoi(argv[1]) : 0;

    DemoApp app;
    if (!app.Create("coregl - mesh collision test")) return 1;

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
    Material* groundMat = scene.create_material(0.35f, 0.4f, 0.45f);
    MeshInstance* groundInst = scene.root().create_child<MeshInstance>("ground");
    groundInst->set_mesh(ground);
    groundInst->set_material(groundMat);

    Mesh* cubeMesh = assets.createCube("cube", 2.f, 2.f, 2.f);
    Material* okMatA = scene.create_material(0.3f, 0.8f, 0.3f);
    Material* okMatB = scene.create_material(0.3f, 0.4f, 0.8f);
    Material* hitMat = scene.create_material(0.9f, 0.15f, 0.1f);

    MeshInstance* cubeA = scene.root().create_child<MeshInstance>("cube_a");
    cubeA->set_mesh(cubeMesh);
    cubeA->set_material(okMatA);

    MeshInstance* cubeB = scene.root().create_child<MeshInstance>("cube_b");
    cubeB->set_mesh(cubeMesh);
    cubeB->set_material(okMatB);

    Mesh* markerMesh = assets.createSphere("contact_marker", 0.25f);
    Material* markerMat = scene.create_material(1.f, 0.9f, 0.1f);
    markerMat->unlit = true;
    MeshInstance* marker = scene.root().create_child<MeshInstance>("contact_marker");
    marker->set_mesh(markerMesh);
    marker->set_material(markerMat);
    marker->visible = false;

    // ── collision registry: add both cubes once, remove is never actually
    // called here (nothing needs to leave the scene), but exercised anyway
    // right after add to prove the API round-trips cleanly ──
    CollisionRegistry registry;
    int handleA = registry.add(cubeMesh, cubeA);
    int handleB = registry.add(cubeMesh, cubeB);
    {
        int scratch = registry.add(cubeMesh, cubeA);
        registry.remove(scratch); // add/remove round-trip smoke test
    }

    Camera3D* camera = scene.root().create_child<Camera3D>("fly");
    camera->set_perspective(55.f, 0.1f, 200.f);
    camera->set_position(0.f, 8.f, 16.f);

    scene.set_active_camera(camera);
    scene.ready();

    FlyCam fly;
    fly.speed = 12.f;
    gl::u64 lastTicks = SDL_GetPerformanceCounter();
    const gl::u64 freq = SDL_GetPerformanceFrequency();

    float t = 0.f;
    bool wasHit = false;
    int hitCount = 0;

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

        // two independent orbits, radii picked so the cubes sweep through
        // each other roughly every couple of seconds instead of either
        // always overlapping or never touching
        t += dt;
        cubeA->set_position(cosf(t * 0.6f) * 3.5f, 1.f, sinf(t * 0.6f) * 3.5f);
        cubeA->set_rotation(Quaternion::FromAxisAngle(Vec3(0.3f, 1.f, 0.f), t * 0.9f));
        cubeB->set_position(-cosf(t * 0.9f + 2.1f) * 2.2f, 1.f, -sinf(-t * 0.9f + 2.1f) * 2.2f);
        cubeB->set_rotation(Quaternion::FromAxisAngle(Vec3(0.f, 1.f, 0.4f), -t * 1.3f));

        scene.update(dt);

        ContactInfo info;
        bool hit = registry.test(handleA, handleB, info);
        cubeA->set_material(hit ? hitMat : okMatA);
        cubeB->set_material(hit ? hitMat : okMatB);
        marker->visible = hit;
        if (hit) marker->set_position(info.point);

        if (hit && !wasHit)
        {
            ++hitCount;
            printf("hit #%d: point=(%.2f,%.2f,%.2f) normal=(%.2f,%.2f,%.2f) depth=%.3f\n", hitCount,
                  info.point.x, info.point.y, info.point.z, info.normal.x, info.normal.y,
                  info.normal.z, info.depth);
        }
        wasHit = hit;

        int w, h;
        app.DrawableSize(&w, &h);
        renderer.render(scene, w, h);
        app.EndFrame();

        ++frame;
        if (maxFrames > 0 && frame >= maxFrames) running = false;
    }

    printf("frames: %d | collisions detected: %d\n", frame, hitCount);
    scene.release_gpu();
    assets.release();
    renderer.release();
    app.Destroy();
    return 0;
}
