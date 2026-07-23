// Minimal repro for the urban tactics shadow peter-panning/light-leak bug.
// Two modes, switched by editing the scene-build block below:
//  - synthetic (current): a ground plane + one SOLID cube (real thickness,
//    closed on all 6 sides) — isolates whether the leak needs a
//    zero-thickness single-sided caster (like the real level's fence) or
//    happens even with ordinary closed geometry.
//  - real level: loads the actual city.obj (see git history/comments
//    below for the load_obj_mesh call) — reproduces the actual artifact
//    directly instead of something that merely resembles it.
// Either way: no character/weapon/particles/collision, free-fly camera
// only, shadow bias/polygon-offset live-tunable at runtime (no rebuild
// needed) so a fix can be found by pressing keys and watching the
// wall/floor contact lines directly.
//
// Controls: WASD move, Q/E down/up, hold left mouse to look, LSHIFT fast,
//           J/K shadow normal bias -/+, U/I polygon offset units -/+,
//           G/H polygon offset factor -/+, M cascade tint (is the seam
//           sitting on a cascade boundary?), N shadows on/off (is this
//           even a shadow artifact at all?), X cycles shadow debug modes
//           (1: magenta = outside cascade box, 2: raw occlusion grayscale
//           — is the shadow TEST itself wrong at the line, or is it
//           introduced later in the lighting math?), F10 gif, ESC quit.
// Usage: demo_shadow_scale_test [numFrames]

#include "demo_app.hpp"
#include "demo_shapes.hpp"
#include <scene/AssetManager.hpp>
#include <scene/Camera3D.hpp>
#include <scene/Material.hpp>
#include <scene/Mesh.hpp>
#include <scene/MeshInstance.hpp>
#include <scene/Scene.hpp>
#include <scene/SceneRenderer.hpp>
#include <cstdio>
#include <cstdlib>
#include <vector>

// vertical, single-sided, zero-thickness quad standing on y=0 (like the
// real level's fence/thin walls) — width along X, height along Y, facing
// +Z. Deliberately NOT a box: this is the specific shape being tested.
static void demoBuildFence(Mesh& mesh, float width, float height)
{
    MeshVertex verts[4];
    const float pos[4][3] = {
        {-width * .5f, 0.f, 0.f}, {width * .5f, 0.f, 0.f},
        {width * .5f, height, 0.f}, {-width * .5f, height, 0.f}};
    for (int i = 0; i < 4; ++i)
    {
        verts[i].position = Vec3(pos[i][0], pos[i][1], pos[i][2]);
        verts[i].normal = Vec3(0.f, 0.f, 1.f);
        verts[i].tangent = Vec4(1.f, 0.f, 0.f, 1.f);
        verts[i].uv = Vec2((float)(i == 1 || i == 2), (float)(i >= 2));
    }
    const u16 idx[6] = {0, 1, 2, 0, 2, 3};
    mesh.set_data(verts, 4, idx, 6);
    mesh.upload();
}

int main(int argc, char** argv)
{
    const int maxFrames = (argc > 1) ? atoi(argv[1]) : 0;

    DemoApp app;
    if (!app.Create("coregl - shadow scale test")) return 1;
    printf("controls: WASD move | Q/E down/up | left mouse look | LSHIFT fast | "
          "J/K normal bias -/+ | U/I polygon offset units -/+ | G/H polygon offset factor -/+ | "
          "M cascade tint | N shadows on/off | X cycle shadow debug modes | C cycle shadow "
          "depth-pass cull mode | Z raw shadow map thumbnail | R reset to opengl-tutorial.org "
          "#16's FRONT-cull fix | F10 gif | ESC\n");

    fs::getFilesystem().addFolder("../../../..");
    fs::getFilesystem().addFolder("../../../../..");

    SceneRenderer renderer;
    // single cascade (not 4): removes cascade-boundary blending as a
    // variable while isolating the bias/offset bug. distance sized to
    // THIS actual test scene (a 20x20 plane, camera 4-10 units from the
    // geometry) instead of urbantactics' 120 (sized for its 188-unit
    // map) — with 1 cascade spanning the camera's full near-far, a
    // needlessly large distance stretches the light-space box far beyond
    // what's actually on screen, spreading both texel resolution and
    // depth precision thin for no reason. No bias value fixes a
    // precision deficit, only reducing the wasted range does.
    //
    // resolution bumped 2048 -> 4096 to directly test whether the
    // remaining artifact is a genuine precision/resolution limit (should
    // visibly shrink) or something structural (won't change no matter
    // how much resolution is thrown at it) — every standard bias/offset/
    // cull-mode/geometry-thickness lever has been ruled out already.
    if (!renderer.init() || !renderer.enable_shadows(1, 1024, 2.5f))
    {
        fprintf(stderr, "renderer init failed\n");
        app.Destroy();
        return 1;
    }
    renderer.set_light_dir(Vec3(-0.35f, -0.75f, -0.45f));
    // zero ambient: only the sun's own direct+shadow term is visible, so
    // shadowed vs lit reads as pure black vs full brightness — no fill
    // light blending the boundary and making it harder to judge exactly
    // where the shadow edge actually falls
    renderer.set_ambient_color(Vec3(0.f, 0.f, 0.f));
    renderer.set_sky_enabled(true);

    float normalBias = 0.01f;
    float polyFactor = 2.5f, polyUnits = 4.f;
    renderer.set_shadow_normal_bias(normalBias);
    renderer.set_shadow_polygon_offset(polyFactor, polyUnits);

    Scene scene;
    assets::AssetManager& assets = assets::AssetManager::instance();

    // real level mesh: loaded (available, e.g. to switch back to by
    // adding a MeshInstance for it) but NOT attached to the scene right
    // now — this test's scene is the synthetic plane+cube below instead
    const char* kAssetDir = "games/urbantactics/assets/";
    std::vector<Material*> levelMats;
    Mesh* levelMesh = assets.load_obj_mesh(
        "city", (std::string(kAssetDir) + "city_edit.obj").c_str(), levelMats, kAssetDir);
    if (!levelMesh)
        fprintf(stderr, "city_edit.obj load failed (non-fatal here — not used this run)\n");

    // synthetic test scene: a ground plane + one SOLID cube (real
    // thickness, closed on all 6 sides, unlike the fence/thin walls in
    // the real level). If this shows no light leak at its base while the
    // real level's fence does, that confirms the leak is specific to
    // zero-thickness single-sided casters, not a general bias/shadow-map
    // problem — isolates the variable directly instead of reasoning
    // about it.
    Mesh* planeMesh = scene.create_mesh();
    demoBuildPlane(*planeMesh, 10.f);
    Mesh* cubeMesh = scene.create_mesh();
    demoBuildCube(*cubeMesh);

    Material* groundMat = scene.create_material(0.72f, 0.72f, 0.70f);
    MeshInstance* ground = scene.root().create_child<MeshInstance>("ground");
    ground->set_mesh(planeMesh);
    ground->set_material(groundMat);

    Material* boxMat = scene.create_material(0.6f, 0.5f, 0.4f);
    MeshInstance* box = scene.root().create_child<MeshInstance>("box");
    box->set_mesh(cubeMesh);
    box->set_material(boxMat);
    box->set_position(2.f, 0.f, -3.f);
    box->set_scale(Vec3(2.f, 1.5f, 2.f)); // human-scale-ish solid block, real thickness

    // same test, but zero-thickness single-sided (like the real fence) —
    // side by side with the solid box above for a direct comparison under
    // identical light/bias/offset settings
    Mesh* fenceMesh = scene.create_mesh();
    demoBuildFence(*fenceMesh, 3.f, 1.5f);
    Material* fenceMat = scene.create_material(0.35f, 0.3f, 0.25f);
    fenceMat->double_sided = true; // single-sided geometry needs this to be visible from behind
    MeshInstance* fence = scene.root().create_child<MeshInstance>("fence");
    fence->set_mesh(fenceMesh);
    fence->set_material(fenceMat);
    fence->set_position(-2.f, 0.f, -3.f);

    // third case, between the two extremes: a real CLOSED box (6 sides,
    // same demoBuildCube as the solid one) but with one dimension
    // squashed to 0.2 — thin, but not zero. Does a modest real thickness
    // already avoid the leak, or does it need to be much thicker at this
    // world scale?
    Material* thinBoxMat = scene.create_material(0.5f, 0.55f, 0.6f);
    MeshInstance* thinBox = scene.root().create_child<MeshInstance>("thin_box");
    thinBox->set_mesh(cubeMesh);
    thinBox->set_material(thinBoxMat);
    thinBox->set_position(0.f, 0.f, -3.f);
    thinBox->set_scale(Vec3(0.2f, 1.5f, 3.f)); // 0.2-unit thickness, same footprint as the fence

    Camera3D* camera = scene.root().create_child<Camera3D>("fly");
    camera->set_perspective(60.f, 0.1f, 400.f);
    camera->set_position(0.f, 1.5f, 4.f); // close, grazing-ish angle at the box's base

    scene.set_active_camera(camera);
    scene.ready();

    float camYaw = 0.f, camPitch = -0.15f;
    bool looking = false;
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
                if (ev.key.keysym.sym == SDLK_j || ev.key.keysym.sym == SDLK_k)
                {
                    normalBias += (ev.key.keysym.sym == SDLK_k) ? 0.005f : -0.005f;
                    if (normalBias < 0.f) normalBias = 0.f;
                    renderer.set_shadow_normal_bias(normalBias);
                    printf("normal bias = %.4f\n", normalBias);
                }
                if (ev.key.keysym.sym == SDLK_u || ev.key.keysym.sym == SDLK_i)
                {
                    polyUnits += (ev.key.keysym.sym == SDLK_i) ? 1.f : -1.f;
                    if (polyUnits < 0.f) polyUnits = 0.f;
                    renderer.set_shadow_polygon_offset(polyFactor, polyUnits);
                    printf("polygon offset factor=%.2f units=%.2f\n", polyFactor, polyUnits);
                }
                if (ev.key.keysym.sym == SDLK_g || ev.key.keysym.sym == SDLK_h)
                {
                    polyFactor += (ev.key.keysym.sym == SDLK_h) ? 0.5f : -0.5f;
                    if (polyFactor < 0.f) polyFactor = 0.f;
                    renderer.set_shadow_polygon_offset(polyFactor, polyUnits);
                    printf("polygon offset factor=%.2f units=%.2f\n", polyFactor, polyUnits);
                }
                if (ev.key.keysym.sym == SDLK_m)
                {
                    static bool showCascades = false;
                    showCascades = !showCascades;
                    renderer.set_show_cascades(showCascades);
                    printf("cascade tint: %s (reveals whether the seam sits on a cascade "
                          "boundary)\n",
                          showCascades ? "on" : "off");
                }
                if (ev.key.keysym.sym == SDLK_n)
                {
                    static bool shadowsActive = true;
                    shadowsActive = !shadowsActive;
                    renderer.set_shadows_active(shadowsActive);
                    printf("shadows: %s\n", shadowsActive ? "on" : "off");
                }
                if (ev.key.keysym.sym == SDLK_x)
                {
                    static int debugMode = 0;
                    debugMode = (debugMode + 1) % 3;
                    renderer.set_debug_shadow_clip(debugMode);
                    const char* names[3] = {
                        "off", "1: magenta = outside cascade box",
                        "2: raw occlusion grayscale (black=lit, white=shadowed)"};
                    printf("shadow debug mode: %s\n", names[debugMode]);
                }
                if (ev.key.keysym.sym == SDLK_c)
                {
                    static int cullMode = 0; // 0=NONE, 1=BACK, 2=FRONT
                    cullMode = (cullMode + 1) % 3;
                    gl::CullMode modes[3] = {gl::CullMode::NONE, gl::CullMode::BACK,
                                             gl::CullMode::FRONT};
                    const char* names[3] = {"NONE (both faces write depth)",
                                            "BACK (only front faces write depth)",
                                            "FRONT (only back faces write depth)"};
                    renderer.set_shadow_cull_mode(modes[cullMode]);
                    printf("shadow depth-pass cull mode: %s\n", names[cullMode]);
                }
                if (ev.key.keysym.sym == SDLK_z)
                {
                    static bool showMap = false;
                    showMap = !showMap;
                    renderer.set_show_shadow_map(showMap);
                    printf("shadow map thumbnail (top-left, raw depth): %s\n",
                          showMap ? "on" : "off");
                }
                if (ev.key.keysym.sym == SDLK_r)
                {
                    // the opengl-tutorial.org #16 fix, tested in isolation:
                    // FRONT cull in the depth pass (only back faces write
                    // depth — absorbs peter-panning into the caster's own
                    // volume) needs its own bias, not whatever G/H/U/I
                    // were left at from earlier experiments stacked on top
                    normalBias = 0.f;
                    polyFactor = 2.5f;
                    polyUnits = 4.f;
                    renderer.set_shadow_normal_bias(normalBias);
                    renderer.set_shadow_polygon_offset(polyFactor, polyUnits);
                    renderer.set_shadow_cull_mode(gl::CullMode::FRONT);
                    printf("reset: FRONT cull, normal bias=0, polygon offset factor=2.5 "
                          "units=4 (opengl-tutorial.org #16's fix, tested clean)\n");
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
        float speed = keys[SDL_SCANCODE_LSHIFT] ? 20.f : 5.f;
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

    printf("frames: %d\n", frame);

    scene.release_gpu();
    renderer.release();
    app.Destroy();
    return 0;
}
