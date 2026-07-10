// Cascaded shadow maps through the scene: a big ground plane, dozens of
// boxes, one sun. Game code builds the tree and calls render() — the
// cascade fitting, the depth views and the PCF filtering all live inside
// the SceneRenderer. No naked new (create_child / create_mesh /
// create_material) and one release_gpu() call at the end.
//
// Controls: WASD move, Q/E down/up, hold left mouse to look, LSHIFT fast,
//           C cascade tint, F10 gif, ESC quit.
// Usage: demo_shadows [numFrames]   (numFrames > 0: run N frames and exit)

#include "demo_app.hpp"
#include "demo_shapes.hpp"
#include <scene/Scene.hpp>
#include <scene/SceneRenderer.hpp>
#include <scene/MeshInstance.hpp>
#include <scene/Material.hpp>
#include <scene/Camera3D.hpp>
#include <scene/LightNode.hpp>

int main(int argc, char** argv)
{
    const int maxFrames = (argc > 1) ? atoi(argv[1]) : 0;

    DemoApp app;
    if (!app.Create("coregl - cascaded shadows")) return 1;
    printf("controls: WASD move | Q/E down/up | left mouse look | LSHIFT fast | C cascade tint | "
           "F10 gif\n");

    SceneRenderer renderer;
    if (!renderer.init() || !renderer.enable_shadows(4, 2048, 200.f))
    {
        fprintf(stderr, "renderer init failed\n");
        app.Destroy();
        return 1;
    }
    renderer.set_light_dir(Vec3(0.35f, -0.9f, -0.45f)); // sun behind the camera

    // --- the tree; every resource is owned by the scene ---
    Scene scene;

    Mesh* planeMesh = scene.create_mesh();
    demoBuildPlane(*planeMesh, 100.f);
    Mesh* cubeMesh = scene.create_mesh();
    demoBuildCube(*cubeMesh);

    Material* groundMat = scene.create_material(0.72f, 0.72f, 0.70f);
    groundMat->specular = 0.08f;

    MeshInstance* ground = scene.root().create_child<MeshInstance>("ground");
    ground->set_mesh(planeMesh);
    ground->set_material(groundMat);

    // boxes scattered with a deterministic LCG; a small palette of shared
    // materials, some of them shiny to show blinn-phong under the sun
    Material* palette[5];
    palette[0] = scene.create_material(0.78f, 0.42f, 0.32f);
    palette[1] = scene.create_material(0.36f, 0.55f, 0.75f);
    palette[2] = scene.create_material(0.48f, 0.66f, 0.38f);
    palette[3] = scene.create_material(0.80f, 0.70f, 0.36f);
    palette[4] = scene.create_material(0.62f, 0.48f, 0.70f);
    palette[1]->specular = 0.5f;
    palette[3]->specular = 0.8f;
    palette[3]->shininess = 96.f;

    // a tower at the origin: the orbiting point light throws its moving
    // shadow off it, and the spot aims at it
    MeshInstance* tower = scene.root().create_child<MeshInstance>("tower");
    tower->set_mesh(cubeMesh);
    tower->set_material(palette[0]);
    tower->set_scale(Vec3(3.f, 9.f, 3.f));

    gl::u32 rng = 1234;
    for (int i = 0; i < 48; ++i)
    {
        rng = rng * 1664525u + 1013904223u;
        float x = ((rng >> 8 & 1023) / 1023.f) * 160.f - 80.f;
        rng = rng * 1664525u + 1013904223u;
        float z = ((rng >> 8 & 1023) / 1023.f) * 160.f - 80.f;
        rng = rng * 1664525u + 1013904223u;
        float sxz = 0.8f + ((rng >> 8 & 255) / 255.f) * 2.2f;
        rng = rng * 1664525u + 1013904223u;
        float sy = 1.0f + ((rng >> 8 & 255) / 255.f) * 5.0f;

        MeshInstance* box = scene.root().create_child<MeshInstance>("box");
        box->set_mesh(cubeMesh);
        box->set_material(palette[i % 5]);
        box->set_position(x, 0.f, z);
        box->set_scale(Vec3(sxz, sy, sxz));
    }

    // local lights: an orbiting warm point light and a fixed spot, both with
    // their own shadow maps — just nodes, the renderer does the rest
    PointLight* orb = scene.root().create_child<PointLight>("orb");
    orb->color = Vec3(1.f, 0.55f, 0.25f);
    orb->intensity = 60.f;
    orb->range = 30.f;
    orb->cast_shadows = true;

    SpotLight* spot = scene.root().create_child<SpotLight>("spot");
    spot->color = Vec3(0.4f, 0.7f, 1.f);
    spot->intensity = 700.f;
    spot->range = 80.f;
    spot->inner_angle = 0.30f;
    spot->outer_angle = 0.45f;
    spot->cast_shadows = true;
    spot->set_position(-16.f, 14.f, 20.f);
    spot->look_at(Vec3(0.f, 0.f, 0.f)); // at the tower

    Camera3D* camera = scene.root().create_child<Camera3D>("fly");
    camera->set_perspective(55.f, 0.1f, 400.f);
    camera->set_position(0.f, 6.f, 24.f);

    scene.set_active_camera(camera);
    scene.ready();

    float camYaw = 0.f, camPitch = -0.18f;
    bool looking = false;
    bool showCascades = getenv("COREGL_SHOW_CASCADES") != nullptr;
    renderer.set_show_cascades(showCascades);
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

        // orbit the point light around the middle of the field
        static float orbAngle = 0.f;
        orbAngle += dt * 0.8f;
        orb->set_position(9.f * cosf(orbAngle), 4.f, 9.f * sinf(orbAngle));

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
