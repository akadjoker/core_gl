// Volume GRID demo — the editable/persistent sibling of demos/volume: same
// rough rock block and tunnel (CSGDifferenceSource: rough rock minus a
// chain of spheres, see CSGNoiseSource for the rough-surface noise), but
// baked ONCE into a GridSource (a real editable voxel grid — see
// VolumeGridSource.hpp) instead of staying a procedural CSG tree.
//
// Left-click carves a new hole into the wall ahead of the camera by
// writing straight into the grid (GridSource::combineWithSource) — O(dig
// radius) per edit, not O(edit count) the way demos/volume's growing
// CSG-node chain is — then pushes the re-marched geometry into the SAME
// Mesh's GPU buffers in place (AssetManager::update_volume_mesh), the same
// upload_dynamic()/update_vertices() path LOD terrain uses. Every frame
// ray-marches the grid along the camera's forward ray (VolumeSource.hpp's
// rayMarch) to preview the dig point/radius as a wireframe sphere
// (SceneRenderer::draw_wire_sphere) before the click commits it. F5/F6
// save/load the whole grid to disk, proving the voxel DATA — not just the
// extracted mesh — can persist.
//
// Controls: WASD/QE + right-mouse-drag look, LSHIFT sprint, left-click
// dig, Ctrl+wheel dig radius, F5 save, F6 load, F10 gif, ESC quit.

#include "demo_app.hpp"

#include <scene/AssetManager.hpp>
#include <scene/Camera3D.hpp>
#include <scene/FreeFlyBehavior.hpp>
#include <scene/Material.hpp>
#include <scene/MeshInstance.hpp>
#include <scene/Scene.hpp>
#include <scene/SceneRenderer.hpp>
#include <scene/VolumeCSGSource.hpp>
#include <scene/VolumeGridSource.hpp>
#include <scene/VolumeSource.hpp>
#include <vector>

int main(int argc, char** argv)
{
    const int maxFrames = (argc > 1) ? atoi(argv[1]) : 0;
    const char* kSaveFile = "cave.vgrid";

    DemoApp app;
    if (!app.Create("coregl - volume grid (editable voxel field)")) return 1;
    printf("controls: WASD/QE + right-mouse look | LSHIFT sprint | left-click dig | Ctrl+wheel dig radius | "
          "F5 save | F6 load | F10 gif | ESC\n");

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
    float digRadius = 2.2f; // Ctrl+wheel adjusts this, clamped to [min, max]

    volume::CSGCubeSource rock(rockMin, rockMax);

    // noise octaves to make the rock surface rough instead of a plain box
    const float freq[2] = {0.35f, 0.9f};
    const float amp[2] = {0.6f, 0.2f};
    volume::CSGNoiseSource roughRock(&rock, freq, amp, 2, 1234ul);

    // a winding tunnel: a chain of overlapping spheres, unioned together.
    // Only used once, to bake the STARTING shape into the grid below — no
    // heap tracking needed beyond this setup, unlike demos/volume's
    // approach where each dig grows the tree.
    std::vector<volume::Source*> setupHeap; // freed right after the bake
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
        setupHeap.push_back(u);
        baseTunnel = u;
    }
    volume::CSGDifferenceSource cave0(&roughRock, baseTunnel);

    // ── the real voxel grid: bake cave0 into it once, then all edits write
    // straight into these cells (GridSource::combineWithSource) instead of
    // growing a CSG tree. Heap-allocated so F6 (load) can swap it wholesale. ──
    int gw = (int)ceilf((queryTo.x - queryFrom.x) / voxelSize) + 1;
    int gh = (int)ceilf((queryTo.y - queryFrom.y) / voxelSize) + 1;
    int gd = (int)ceilf((queryTo.z - queryFrom.z) / voxelSize) + 1;
    printf("baking initial cave into a %dx%dx%d voxel grid...\n", gw, gh, gd);
    volume::GridSource* grid = new volume::GridSource(gw, gh, gd, queryFrom, voxelSize);
    grid->fill(cave0);
    for (volume::Source* s : setupHeap) delete s;
    setupHeap.clear();

    printf("marching cubes...\n");
    Mesh* caveMesh = assets.build_volume_mesh_dynamic("cave_grid", *grid, queryFrom, queryTo, voxelSize, 1.6f);
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
            int gwPix, ghPix;
            app.DrawableSize(&gwPix, &ghPix);
            app.gif.Toggle(gwPix, ghPix);
        }
        if (Input::IsKeyPressed(KEY_F5))
            printf(grid->save(kSaveFile) ? "saved '%s'\n" : "save failed\n", kSaveFile);
        if (Input::IsKeyPressed(KEY_F6))
        {
            if (volume::GridSource* loaded = volume::GridSource::load(kSaveFile))
            {
                delete grid;
                grid = loaded;
                if (assets.update_volume_mesh(caveMesh, *grid, queryFrom, queryTo, voxelSize))
                    printf("loaded '%s'\n", kSaveFile);
                else
                    printf("loaded '%s' but the mesh update failed (capacity?)\n", kSaveFile);
            }
            else
                printf("load failed\n");
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

        // ray-march the LIVE grid every frame — cheap (a bounded walk along
        // one ray, nowhere near marchGrid's cost) — both to preview where a
        // click would dig (drawn below as a wireframe sphere) and, on the
        // click, to actually dig there instead of at a fixed blind
        // distance. See VolumeSource.hpp's rayMarch.
        Ray camRay(camera->get_global_position(), camera->forward());
        Vec3 pick;
        bool hasPick = volume::rayMarch(*grid, camRay, pick, 60.f);

        if (Input::IsMousePressed(MouseButton::LEFT))
        {
            if (!hasPick)
                printf("dig: nothing in view within range\n");
            else
            {
                volume::CSGSphereSource brush(digRadius, pick);
                // combine radius > dig radius: the original Ogre comment on
                // this puts it well — "you might use its radius times two
                // because the density outside of the sphere is needed too"
                grid->combineWithSource(volume::GridSource::Operation::Difference, brush, pick, digRadius * 2.0f);

                if (assets.update_volume_mesh(caveMesh, *grid, queryFrom, queryTo, voxelSize))
                    printf("dig #%d at (%.1f, %.1f, %.1f), radius %.2f\n", ++digCount, pick.x, pick.y, pick.z,
                          digRadius);
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
        // live preview: where a click would dig, and how big — semi-
        // transparent yellow wire sphere, gone the instant nothing's in range
        if (hasPick) renderer.draw_wire_sphere(*camera, pick, digRadius, 255, 220, 60, 160);
        app.EndFrame();

        ++frame;
        if (maxFrames > 0 && frame >= maxFrames) running = false;
    }

    printf("frames: %d\n", frame);
    scene.release_gpu();
    assets.release();
    delete grid;
    renderer.release();
    app.Destroy();
    return 0;
}
