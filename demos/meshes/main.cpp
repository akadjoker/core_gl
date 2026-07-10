// Static mesh loading: a village of houses read from the engine's binary
// mesh format (assets/models/house.mesh, produced by the exporter tool from
// an OBJ — assimp never runs in the engine). Multi-material surfaces map to
// scene Materials built from the file's material table. CSM + procedural
// sky on top.
//
// Controls: WASD move, Q/E down/up, hold left mouse to look, LSHIFT fast,
//           C cascade tint, F10 gif, ESC quit.
// Usage: demo_meshes [numFrames]   (run from the repo root: assets/...)

#include "demo_app.hpp"
#include "demo_shapes.hpp"
#include <scene/Scene.hpp>
#include <scene/SceneRenderer.hpp>
#include <scene/MeshInstance.hpp>
#include <scene/Material.hpp>
#include <scene/Camera3D.hpp>
#include <scene/Mesh.hpp>
#include <scene/MeshLoader.hpp>

int main(int argc, char** argv)
{
    const int maxFrames = (argc > 1) ? atoi(argv[1]) : 0;

    DemoApp app;
    if (!app.Create("coregl - static meshes")) return 1;
    printf("controls: WASD move | Q/E down/up | left mouse look | LSHIFT fast | C cascade tint | "
           "F10 gif\n");

    SceneRenderer renderer;
    if (!renderer.init() || !renderer.enable_shadows(4, 2048, 200.f))
    {
        fprintf(stderr, "renderer init failed\n");
        app.Destroy();
        return 1;
    }
    renderer.set_light_dir(Vec3(0.45f, -0.75f, -0.4f));
    renderer.set_sky_enabled(true);

    Scene scene;

    // ── the loaded mesh: one file, N surfaces, N material slots ──
    Mesh* houseMesh = scene.create_mesh();
    std::vector<MeshLoader::MaterialDesc> matDescs;
    if (!MeshLoader::load("assets/models/house.mesh", *houseMesh, &matDescs))
    {
        fprintf(stderr, "mesh load failed (run from the repo root)\n");
        app.Destroy();
        return 1;
    }
    houseMesh->upload();

    // materials straight from the file's table
    std::vector<Material*> houseMats;
    for (const MeshLoader::MaterialDesc& d : matDescs)
    {
        Material* m = scene.create_material(d.diffuse.x, d.diffuse.y, d.diffuse.z);
        m->specular = (d.specular.x + d.specular.y + d.specular.z) / 3.f;
        m->shininess = d.shininess > 1.f ? d.shininess : 32.f;
        houseMats.push_back(m);
    }

    // ── ground ──
    Mesh* planeMesh = scene.create_mesh();
    demoBuildPlane(*planeMesh, 90.f);
    Material* groundMat = scene.create_material(0.42f, 0.56f, 0.35f);
    MeshInstance* ground = scene.root().create_child<MeshInstance>("ground");
    ground->set_mesh(planeMesh);
    ground->set_material(groundMat);

    // ── the village: many instances of the same loaded mesh ──
    gl::u32 rng = 77;
    for (int i = 0; i < 24; ++i)
    {
        rng = rng * 1664525u + 1013904223u;
        float x = ((rng >> 8 & 1023) / 1023.f) * 140.f - 70.f;
        rng = rng * 1664525u + 1013904223u;
        float z = ((rng >> 8 & 1023) / 1023.f) * 140.f - 70.f;
        rng = rng * 1664525u + 1013904223u;
        float yaw = ((rng >> 8 & 255) / 255.f) * 6.28318f;
        rng = rng * 1664525u + 1013904223u;
        float s = 0.8f + ((rng >> 8 & 255) / 255.f) * 0.9f;

        MeshInstance* house = scene.root().create_child<MeshInstance>("house");
        house->set_mesh(houseMesh);
        house->set_materials(houseMats);
        house->set_position(x, 0.f, z);
        house->set_euler(Vec3(0.f, yaw, 0.f));
        house->set_scale(s);
    }

    Camera3D* camera = scene.root().create_child<Camera3D>("fly");
    camera->set_perspective(55.f, 0.1f, 400.f);
    camera->set_position(0.f, 7.f, 40.f);

    scene.set_active_camera(camera);
    scene.ready();

    float camYaw = 0.f, camPitch = -0.15f;
    bool looking = false;
    bool showCascades = false;
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
                if (ev.key.keysym.sym == SDLK_c)
                {
                    showCascades = !showCascades;
                    renderer.set_show_cascades(showCascades);
                }
                if (ev.key.keysym.sym == SDLK_F10)
                {
                    int gw, gh;
                    app.DrawableSize(&gw, &gh);
                    app.gif.Toggle(gw, gh);
                }
            }
            if (ev.type == SDL_MOUSEBUTTONDOWN && ev.button.button == SDL_BUTTON_LEFT)
                looking = true;
            if (ev.type == SDL_MOUSEBUTTONUP && ev.button.button == SDL_BUTTON_LEFT)
                looking = false;
            if (ev.type == SDL_MOUSEMOTION && looking)
            {
                camYaw -= (float)ev.motion.xrel * 0.005f;
                camPitch -= (float)ev.motion.yrel * 0.005f;
                if (camPitch > 1.55f) camPitch = 1.55f;
                if (camPitch < -1.55f) camPitch = -1.55f;
            }
        }
        if (!running) break;

        gl::u64 now = SDL_GetPerformanceCounter();
        float dt = (float)((double)(now - lastTicks) / (double)freq);
        lastTicks = now;
        if (dt > 0.1f) dt = 0.1f;

        camera->set_euler(Vec3(camPitch, camYaw, 0.f));
        const Uint8* keys = SDL_GetKeyboardState(nullptr);
        float speed = keys[SDL_SCANCODE_LSHIFT] ? 40.f : 12.f;
        if (keys[SDL_SCANCODE_W]) camera->advance(speed * dt);
        if (keys[SDL_SCANCODE_S]) camera->advance(-speed * dt);
        if (keys[SDL_SCANCODE_A]) camera->strafe(-speed * dt);
        if (keys[SDL_SCANCODE_D]) camera->strafe(speed * dt);
        if (keys[SDL_SCANCODE_Q]) camera->move_global(Vec3(0.f, -speed * dt, 0.f));
        if (keys[SDL_SCANCODE_E]) camera->move_global(Vec3(0.f, speed * dt, 0.f));

        scene.update(dt);

        int w, h;
        app.DrawableSize(&w, &h);
        renderer.render(scene, w, h);

        app.EndFrame();

        ++frame;
        if (maxFrames > 0 && frame >= maxFrames) running = false;
    }

    printf("frames: %d | items/frame: %d\n", frame, renderer.last_item_count());

    scene.release_gpu();
    renderer.release();
    app.Destroy();
    return 0;
}
