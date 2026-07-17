// Volume mesh demo: a big rocky block with a winding cave/tunnel carved
// through it (CSGDifferenceSource: rough rock minus a chain of spheres,
// see CSGNoiseSource for the rough-surface noise), extracted via marching
// cubes (AssetManager::build_volume_mesh_dynamic). Press G to dig a new
// hole into the wall ahead of the camera — instead of building a whole new
// Mesh, that pushes the re-marched geometry into the SAME Mesh's GPU
// buffers in place (AssetManager::update_volume_mesh), the same
// upload_dynamic()/update_vertices() path LOD terrain uses. Every frame
// ray-marches the density field along the camera's forward ray
// (VolumeSource.hpp's rayMarch) to preview the dig point/radius as a
// wireframe sphere (SceneRenderer::draw_wire_sphere) before G commits it.
//
// Controls: WASD/QE + right-mouse-drag look, LSHIFT sprint, G dig,
// Ctrl+wheel dig radius, F10 gif, ESC quit.

#include "demo_app.hpp"

#include <scene/AssetManager.hpp>
#include <scene/Camera3D.hpp>
#include <scene/FreeFlyBehavior.hpp>
#include <scene/Material.hpp>
#include <scene/MeshInstance.hpp>
#include <scene/Scene.hpp>
#include <scene/SceneRenderer.hpp>
#include <scene/VolumeCSGSource.hpp>
#include <scene/VolumeSource.hpp>
#include <vector>

int main(int argc, char** argv)
{
    const int maxFrames = (argc > 1) ? atoi(argv[1]) : 0;

    DemoApp app;
    if (!app.Create("coregl - volume (marching cubes caves)")) return 1;
    printf("controls: WASD/QE + right-mouse look | LSHIFT sprint | G dig | Ctrl+wheel dig radius | F10 gif | "
          "ESC\n");

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

    // ── density field: a rough rock block with a winding tunnel through it ──
    const Vec3 rockMin(-16.f, -6.f, -8.f), rockMax(16.f, 6.f, 8.f);
    const Vec3 queryFrom(-17.5f, -7.5f, -9.5f), queryTo(17.5f, 7.5f, 9.5f);
    const float voxelSize = 0.4f;
    const float kDigRadiusMin = 0.8f, kDigRadiusMax = 5.0f;
    float digRadius = 2.2f; // mouse wheel adjusts this, clamped to [min, max]

    volume::CSGCubeSource rock(rockMin, rockMax);

    // noise octaves to make the rock surface rough instead of a plain box
    const float freq[2] = {0.35f, 0.9f};
    const float amp[2] = {0.6f, 0.2f};
    volume::CSGNoiseSource roughRock(&rock, freq, amp, 2, 1234ul);

    // a winding tunnel: a chain of overlapping spheres, unioned together.
    // Heap-allocated and tracked in digHeap (declared below) since the
    // chain is folded left-to-right — each CSGUnionSource needs its
    // predecessor's address to already be stable, which a self-referencing
    // array initializer can't guarantee as cleanly as this loop can.
    std::vector<volume::Source*> digHeap; // dig spheres/unions; freed at shutdown
    volume::CSGSphereSource tunnelSpheres[] = {
        volume::CSGSphereSource(1.9f, Vec3(-15.f, 0.f, 0.f)),   volume::CSGSphereSource(1.9f, Vec3(-11.f, 1.0f, -1.2f)),
        volume::CSGSphereSource(2.0f, Vec3(-7.f, 1.6f, 0.8f)),  volume::CSGSphereSource(1.9f, Vec3(-3.f, 0.6f, -1.2f)),
        volume::CSGSphereSource(2.0f, Vec3(1.f, -0.6f, 1.0f)),  volume::CSGSphereSource(1.9f, Vec3(5.f, -1.4f, -0.6f)),
        volume::CSGSphereSource(2.0f, Vec3(9.f, -0.6f, 0.8f)),  volume::CSGSphereSource(1.9f, Vec3(13.f, 0.4f, -0.4f)),
        volume::CSGSphereSource(1.9f, Vec3(15.5f, 0.f, 0.2f)),
    };
    constexpr int kTunnelSpheres = sizeof(tunnelSpheres) / sizeof(tunnelSpheres[0]);
    const volume::Source* baseTunnel = &tunnelSpheres[0];
    for (int i = 1; i < kTunnelSpheres; ++i)
    {
        auto* u = new volume::CSGUnionSource(baseTunnel, &tunnelSpheres[i]);
        digHeap.push_back(u);
        baseTunnel = u;
    }

    printf("marching cubes...\n");
    volume::CSGDifferenceSource cave0(&roughRock, baseTunnel);
    Mesh* caveMesh = assets.build_volume_mesh_dynamic("cave", cave0, queryFrom, queryTo, voxelSize, 1.6f);
    if (!caveMesh)
    {
        fprintf(stderr, "build_volume_mesh_dynamic produced no geometry\n");
        app.Destroy();
        return 1;
    }
    printf("cave mesh: capacity %u verts, %u idx (dynamic — see AssetManager::build_volume_mesh_dynamic)\n",
           caveMesh->vertex_count(), caveMesh->index_count());

    Material* rockMat = scene.create_material(0.45f, 0.4f, 0.35f);
    rockMat->double_sided = true; // caves are hollow, so backfaces are visible
    MeshInstance* caveInst = scene.root().create_child<MeshInstance>("cave");
    caveInst->set_mesh(caveMesh);
    caveInst->set_material(rockMat);

    // ── digging: G carves a new sphere into the wall ahead of the camera,
    // updating caveMesh's GPU buffers in place instead of building a new
    // Mesh. `tunnelNow` is a running accumulator — each dig wraps it in ONE
    // new union with the new sphere, rather than rebuilding the whole chain
    // from baseTunnel every time (that earlier version re-allocated a fresh
    // duplicate union per PREVIOUS dig on every single new dig — an
    // avoidable O(digCount^2) pile of immediately-orphaned nodes). Every
    // node created here stays live in the tree, so digHeap holds no garbage
    // to clean up beyond the normal end-of-program free. No cap on dig
    // count — the real limit is the GPU capacity reserved by
    // build_volume_mesh_dynamic's capacityFactor; update_volume_mesh fails
    // cleanly (no change, logged) once a dig's geometry would exceed it. ──
    const volume::Source* tunnelNow = baseTunnel;
    int digCount = 0;

    // ── camera: free-fly controller as a child node (see FreeFlyBehavior.hpp) ──
    Camera3D* camera = scene.root().create_child<Camera3D>("camera");
    camera->set_perspective(60.f, 0.1f, 300.f);
    camera->set_position(-14.f, 1.f, 6.f);
    camera->create_child<FreeFlyBehavior>();

    scene.set_active_camera(camera);
    scene.ready();

    gl::u64 lastTicks = SDL_GetPerformanceCounter();
    const gl::u64 freq_ = SDL_GetPerformanceFrequency();

    int frame = 0;
    bool running = true;
    while (running)
    {
        if (!app.PollEvents()) break;
        if (Input::IsKeyPressed(KEY_ESCAPE)) break;
        if (Input::IsKeyPressed(KEY_F10))
        {
            int gw, gh;
            app.DrawableSize(&gw, &gh);
            app.gif.Toggle(gw, gh);
        }
        // Ctrl+wheel adjusts the dig radius, clamped to [kDigRadiusMin,
        // kDigRadiusMax] — plain wheel is already FreeFlyBehavior's fly
        // speed, so this needs its own modifier or every scroll would
        // change both at once.
        float wheel = Input::GetMouseWheelMoveV();
        if (wheel != 0.f && Input::IsKeyDown(KEY_LEFT_CONTROL))
        {
            digRadius = Clamp(digRadius + wheel * 0.25f, kDigRadiusMin, kDigRadiusMax);
            printf("dig radius: %.2f\n", digRadius);
        }

        // ray-march the CURRENT density field (rock minus every dig so far)
        // every frame — cheap (a bounded walk along one ray, nowhere near
        // marchGrid's cost) — both to preview where G would dig (drawn
        // below as a wireframe sphere) and, on the keypress, to actually
        // dig there instead of at a fixed blind distance. See
        // VolumeSource.hpp's rayMarch.
        volume::CSGDifferenceSource currentCave(&roughRock, tunnelNow);
        Ray camRay(camera->get_global_position(), camera->forward());
        Vec3 pick;
        bool hasPick = volume::rayMarch(currentCave, camRay, pick, 60.f);

        if (Input::IsMousePressed(MouseButton::LEFT))
        {
            if (!hasPick)
                printf("dig: nothing in view within range\n");
            else
            {
                auto* sphere = new volume::CSGSphereSource(digRadius, pick);
                auto* unioned = new volume::CSGUnionSource(tunnelNow, sphere);
                digHeap.push_back(sphere);
                digHeap.push_back(unioned);

                volume::CSGDifferenceSource newCave(&roughRock, unioned);
                if (assets.update_volume_mesh(caveMesh, newCave, queryFrom, queryTo, voxelSize))
                {
                    tunnelNow = unioned; // only commit the accumulator on success
                    printf("dig #%d at (%.1f, %.1f, %.1f), radius %.2f\n", ++digCount, pick.x, pick.y, pick.z,
                          digRadius);
                }
                else
                    printf("dig exceeded reserved capacity — no change\n");
            }
        }

        gl::u64 now = SDL_GetPerformanceCounter();
        float dt = (float)((double)(now - lastTicks) / (double)freq_);
        lastTicks = now;
        if (dt > 0.1f) dt = 0.1f;

        scene.update(dt);

        int w, h;
        app.DrawableSize(&w, &h);
        renderer.render(scene, w, h);
        // live preview: where G would dig, and how big — semi-transparent
        // yellow wire sphere, gone the instant nothing's in range
        if (hasPick) renderer.draw_wire_sphere(*camera, pick, digRadius, 255, 220, 60, 160);
        app.EndFrame();

        ++frame;
        if (maxFrames > 0 && frame >= maxFrames) running = false;
    }

    printf("frames: %d\n", frame);
    scene.release_gpu();
    assets.release();
    for (volume::Source* s : digHeap) delete s;
    renderer.release();
    app.Destroy();
    return 0;
}
