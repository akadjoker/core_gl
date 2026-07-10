// Ocean demo: an OceanNode driven by the Gerstner-wave shader
// (assets/shaders/water.ps|.fs, embedded in the scene library), around a
// brute-force island terrain — the terrain/water contact line is where the
// shore foam and depth effects prove themselves. Written like final user
// code: build the tree, call render().
//
// Controls: WASD move, Q/E down/up, hold left mouse to look, LSHIFT fast,
//           V debug views, F10 gif, ESC quit.
// Usage: demo_ocean [numFrames]

#include "demo_app.hpp"
#include "demo_shapes.hpp"
#include <scene/Scene.hpp>
#include <scene/SceneRenderer.hpp>
#include <scene/MeshInstance.hpp>
#include <scene/Material.hpp>
#include <scene/Camera3D.hpp>
#include <scene/OceanNode.hpp>
#include <scene/Mesh.hpp>
#include <scene/Pixmap.hpp>
#include <scene/AssetManager.hpp>
#include <vector>

// tileable bump map: rg = two phase-shifted sine fields (integer cycle
// counts keep the texture seamless under REPEAT)
static void buildBumpTexture(gl::Texture& tex, int size)
{
    scene::Pixmap pix(size, size, 4);
    const float tau = 6.28318530f;
    for (int j = 0; j < size; ++j)
    {
        for (int i = 0; i < size; ++i)
        {
            float x = (float)i / size, y = (float)j / size;
            float r = 0.5f + 0.17f * sinf(tau * (3.f * x + 1.f * y)) +
                      0.17f * sinf(tau * (7.f * x - 4.f * y) + 1.3f) +
                      0.16f * sinf(tau * (11.f * y + 2.f * x) + 4.1f);
            float g = 0.5f + 0.17f * sinf(tau * (2.f * y - 5.f * x) + 0.7f) +
                      0.17f * sinf(tau * (6.f * y + 3.f * x) + 2.9f) +
                      0.16f * sinf(tau * (9.f * x - 8.f * y) + 5.2f);
            pix.set_pixel((gl::u32)i, (gl::u32)j, (gl::u8)(r * 255.f), (gl::u8)(g * 255.f), 128,
                          255);
        }
    }
    tex.Load2D(pix.pixels, size, size, gl::TextureFormat::RGBA8);
    tex.SetFilter(gl::TextureFilter::LINEAR, gl::TextureFilter::LINEAR);
    tex.SetWrap(gl::TextureWrap::REPEAT, gl::TextureWrap::REPEAT);
}

// tileable blotchy foam pattern in r (the shader thresholds it)
static void buildFoamTexture(gl::Texture& tex, int size)
{
    scene::Pixmap pix(size, size, 4);
    const float tau = 6.28318530f;
    for (int j = 0; j < size; ++j)
    {
        for (int i = 0; i < size; ++i)
        {
            float x = (float)i / size, y = (float)j / size;
            float v = sinf(tau * (4.f * x + 2.f * y)) * sinf(tau * (3.f * y - 5.f * x) + 1.1f) +
                      sinf(tau * (8.f * x - 3.f * y) + 2.3f) * sinf(tau * (6.f * y + x) + 4.7f);
            float f = 0.5f + 0.25f * v; // ~0..1, blobby
            gl::u8 c = (gl::u8)(255.f * (f < 0.f ? 0.f : (f > 1.f ? 1.f : f)));
            pix.set_pixel((gl::u32)i, (gl::u32)j, c, c, c, 255);
        }
    }
    tex.Load2D(pix.pixels, size, size, gl::TextureFormat::RGBA8);
    tex.SetFilter(gl::TextureFilter::LINEAR, gl::TextureFilter::LINEAR);
    tex.SetWrap(gl::TextureWrap::REPEAT, gl::TextureWrap::REPEAT);
}

// ── brute-force island terrain: the water/terrain contact line is where
// shore foam and depth-based effects prove themselves ──

static float islandHeight(float x, float z)
{
    float r2 = x * x + z * z;
    float h = 26.f * expf(-r2 / (110.f * 110.f)) - 9.f; // dome rising from the sea floor
    h += 2.2f * sinf(x * 0.045f) * cosf(z * 0.05f);
    h += 1.1f * sinf(x * 0.11f + 1.7f) * sinf(z * 0.09f + 0.6f);
    return h;
}

static void buildIsland(Mesh& mesh, int cells, float half)
{
    const int n = cells + 1;
    const float step = (2.f * half) / cells;
    std::vector<MeshVertex> verts((size_t)n * n);
    for (int j = 0; j < n; ++j)
        for (int i = 0; i < n; ++i)
        {
            float x = -half + i * step, z = -half + j * step;
            MeshVertex& v = verts[(size_t)j * n + i];
            v.position = Vec3(x, islandHeight(x, z), z);
            v.normal = Vec3(0.f, 1.f, 0.f);
            v.tangent = Vec4(1.f, 0.f, 0.f, 1.f);
            v.uv = Vec2((float)i / cells, (float)j / cells);
        }
    std::vector<u32> idx;
    idx.reserve((size_t)cells * cells * 6);
    for (int j = 0; j < cells; ++j)
        for (int i = 0; i < cells; ++i)
        {
            u32 a = (u32)(j * n + i), b = a + 1, d = a + (u32)n, c = d + 1;
            idx.push_back(a);
            idx.push_back(c);
            idx.push_back(b); // CCW from above
            idx.push_back(a);
            idx.push_back(d);
            idx.push_back(c);
        }
    mesh.set_data(verts.data(), (u32)verts.size(), idx.data(), (u32)idx.size());
    mesh.compute_normals();
    mesh.upload();
}

// height-colored texture: sea bed, sand at the waterline, grass above
static void buildIslandTexture(gl::Texture& tex, int size, float half)
{
    scene::Pixmap pix(size, size, 4);
    for (int j = 0; j < size; ++j)
        for (int i = 0; i < size; ++i)
        {
            float x = -half + 2.f * half * i / (size - 1);
            float z = -half + 2.f * half * j / (size - 1);
            float h = islandHeight(x, z);
            gl::u8 r, g, b;
            if (h < -1.5f)
            {
                r = 120;
                g = 116;
                b = 96;
            } // sea bed
            else if (h < 1.2f)
            {
                r = 214;
                g = 196;
                b = 148;
            } // beach sand
            else if (h < 6.f)
            {
                r = 96;
                g = 150;
                b = 74;
            } // grass
            else
            {
                r = 110;
                g = 104;
                b = 92;
            } // rock
            pix.set_pixel((gl::u32)i, (gl::u32)j, r, g, b, 255);
        }
    tex.Load2D(pix.pixels, size, size, gl::TextureFormat::RGBA8);
    tex.SetFilter(gl::TextureFilter::LINEAR, gl::TextureFilter::LINEAR);
    tex.SetWrap(gl::TextureWrap::CLAMP_TO_EDGE, gl::TextureWrap::CLAMP_TO_EDGE);
}

int main(int argc, char** argv)
{
    const int maxFrames = (argc > 1) ? atoi(argv[1]) : 0;

    DemoApp app;
    if (!app.Create("coregl - ocean")) return 1;
    printf("controls: WASD move | Q/E down/up | left mouse look | LSHIFT fast | V debug views | "
           "F10 gif\n");

    SceneRenderer renderer;
    if (!renderer.init())
    {
        fprintf(stderr, "renderer init failed\n");
        app.Destroy();
        return 1;
    }
    renderer.set_light_dir(Vec3(0.4f, -0.6f, -0.4f));
    renderer.set_clear_color(0.62f, 0.74f, 0.88f);

    Scene scene;
    assets::AssetManager& assets = assets::AssetManager::instance();

    // real textures when available (run from the repo root); procedural
    // stand-ins otherwise
    gl::Texture* bumpTex = assets.loadTexture("ocean_bump", "assets/textures/waterbump.png");
    if (!bumpTex)
    {
        bumpTex = assets.createTexture("ocean_bump");
        buildBumpTexture(*bumpTex, 256);
    }
    gl::Texture* foamTex = assets.loadTexture("ocean_foam", "assets/textures/foam.png");
    if (!foamTex)
    {
        foamTex = assets.createTexture("ocean_foam");
        buildFoamTexture(*foamTex, 256);
    }

    // the island: a brute-force heightmap crossing the waterline
    Mesh* islandMesh = scene.create_mesh();
    buildIsland(*islandMesh, 220, 200.f);
    gl::Texture* islandTex = assets.createTexture("island_diffuse");
    buildIslandTexture(*islandTex, 512, 200.f);
    Material* islandMat = scene.create_material();
    islandMat->diffuse = islandTex;

    MeshInstance* island = scene.root().create_child<MeshInstance>("island");
    island->set_mesh(islandMesh);
    island->set_material(islandMat);

    // a couple of rocks on the beach for extra reflections
    Mesh* cubeMesh = scene.create_mesh();
    demoBuildCube(*cubeMesh);
    Material* rockMat = scene.create_material(0.5f, 0.47f, 0.42f);
    const float rocks[][2] = {{-92.f, 30.f}, {70.f, -78.f}, {40.f, 96.f}};
    for (int i = 0; i < 3; ++i)
    {
        MeshInstance* rock = scene.root().create_child<MeshInstance>("rock");
        rock->set_mesh(cubeMesh);
        rock->set_material(rockMat);
        rock->set_position(rocks[i][0], islandHeight(rocks[i][0], rocks[i][1]) - 0.5f, rocks[i][1]);
        rock->set_scale(Vec3(5.f, 4.f, 5.f));
    }

    // the ocean itself: one node
    OceanNode* ocean = scene.root().create_child<OceanNode>("ocean");
    ocean->set_size(400.f);
    ocean->set_position(0.f, 0.f, 0.f);
    ocean->bump = bumpTex;
    ocean->foam = foamTex;

    Camera3D* camera = scene.root().create_child<Camera3D>("fly");
    // the ocean shader linearizes depth with near 0.1 / far 1000
    camera->set_perspective(55.f, 0.1f, 1000.f);
    camera->set_position(0.f, 18.f, 170.f);

    scene.set_active_camera(camera);
    scene.ready();

    float camYaw = 0.f, camPitch = -0.15f;
    bool looking = false;
    bool debugViews = false;
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
                if (ev.key.keysym.sym == SDLK_v)
                {
                    debugViews = !debugViews;
                    renderer.set_debug_views(debugViews);
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
        float speed = keys[SDL_SCANCODE_LSHIFT] ? 60.f : 18.f;
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
    assets.release();
    renderer.release();
    app.Destroy();
    return 0;
}
