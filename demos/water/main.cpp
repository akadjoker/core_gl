// Water demo: a brute-force heightmap terrain, scattered landmark cubes and
// a WaterNode. Written like final user code — the whole frame is
// scene.update() + renderer.render(); the reflection/refraction views happen
// inside the SceneRenderer, this file never calls gl:: for drawing.
//
// Controls: WASD move, Q/E down/up, hold left mouse to look, LSHIFT fast,
//           F10 gif, ESC quit.
// Usage: demo_water [numFrames]   (numFrames > 0: run N frames and exit)

#include "demo_app.hpp"
#include "demo_shapes.hpp"
#include <scene/Scene.hpp>
#include <scene/SceneRenderer.hpp>
#include <scene/MeshInstance.hpp>
#include <scene/Material.hpp>
#include <scene/Camera3D.hpp>
#include <scene/WaterNode.hpp>
#include <scene/Pixmap.hpp>
#include <scene/AssetManager.hpp>

// ── terrain ──────────────────────────────────────────────────────────────

static float terrainHeight(float x, float z)
{
    float h = 0.f;
    h += 5.5f * sinf(x * 0.045f) * cosf(z * 0.038f);
    h += 2.6f * sinf(x * 0.11f + 1.7f) * sinf(z * 0.09f + 0.6f);
    h += 1.1f * sinf(x * 0.23f + 4.1f) * cosf(z * 0.21f + 2.3f);
    return h;
}

// color ramp by height: sand near the waterline, grass, rock, snow
static void heightColor(float h, gl::u8* rgb)
{
    struct Stop
    {
        float h;
        gl::u8 r, g, b;
    };
    static const Stop stops[] = {
        {-9.f, 150, 134, 96},  // deep bed
        {0.3f, 206, 188, 140}, // sand
        {2.0f, 96, 148, 74},   // grass
        {5.0f, 72, 112, 58},   // dark grass
        {7.5f, 122, 110, 98},  // rock
        {10.f, 226, 228, 231}, // snow
    };
    const int n = (int)(sizeof(stops) / sizeof(stops[0]));
    if (h <= stops[0].h)
    {
        rgb[0] = stops[0].r, rgb[1] = stops[0].g, rgb[2] = stops[0].b;
        return;
    }
    for (int i = 1; i < n; ++i)
    {
        if (h <= stops[i].h)
        {
            float t = (h - stops[i - 1].h) / (stops[i].h - stops[i - 1].h);
            rgb[0] = (gl::u8)(stops[i - 1].r + t * (stops[i].r - stops[i - 1].r));
            rgb[1] = (gl::u8)(stops[i - 1].g + t * (stops[i].g - stops[i - 1].g));
            rgb[2] = (gl::u8)(stops[i - 1].b + t * (stops[i].b - stops[i - 1].b));
            return;
        }
    }
    rgb[0] = stops[n - 1].r, rgb[1] = stops[n - 1].g, rgb[2] = stops[n - 1].b;
}

// brute force: one big grid mesh, every vertex from the height function
static void buildTerrain(Mesh& mesh, int cells, float half)
{
    const int n = cells + 1;
    const float step = (2.f * half) / cells;

    std::vector<MeshVertex> verts((size_t)n * n);
    for (int j = 0; j < n; ++j)
    {
        for (int i = 0; i < n; ++i)
        {
            float x = -half + i * step;
            float z = -half + j * step;
            MeshVertex& v = verts[(size_t)j * n + i];
            v.position = Vec3(x, terrainHeight(x, z), z);
            v.normal = Vec3(0.f, 1.f, 0.f); // recomputed below
            v.tangent = Vec4(1.f, 0.f, 0.f, 1.f);
            v.uv = Vec2((float)i / cells, (float)j / cells);
        }
    }

    std::vector<u32> idx;
    idx.reserve((size_t)cells * cells * 6);
    for (int j = 0; j < cells; ++j)
    {
        for (int i = 0; i < cells; ++i)
        {
            u32 a = (u32)(j * n + i);
            u32 b = a + 1;
            u32 d = a + (u32)n;
            u32 c = d + 1;
            // CCW seen from above
            idx.push_back(a);
            idx.push_back(c);
            idx.push_back(b);
            idx.push_back(a);
            idx.push_back(d);
            idx.push_back(c);
        }
    }

    mesh.set_data(verts.data(), (u32)verts.size(), idx.data(), (u32)idx.size());
    mesh.compute_normals();
    mesh.upload();
}

// bakes the height color ramp into a texture that maps 1:1 over the terrain
static void buildTerrainTexture(gl::Texture& tex, int size, float half)
{
    scene::Pixmap pix(size, size, 4);
    for (int j = 0; j < size; ++j)
    {
        for (int i = 0; i < size; ++i)
        {
            float x = -half + (2.f * half) * i / (size - 1);
            float z = -half + (2.f * half) * j / (size - 1);
            gl::u8 rgb[3];
            heightColor(terrainHeight(x, z), rgb);
            pix.set_pixel((gl::u32)i, (gl::u32)j, rgb[0], rgb[1], rgb[2], 255);
        }
    }
    tex.Load2D(pix.pixels, size, size, gl::TextureFormat::RGBA8);
    tex.SetFilter(gl::TextureFilter::LINEAR, gl::TextureFilter::LINEAR);
    tex.SetWrap(gl::TextureWrap::CLAMP_TO_EDGE, gl::TextureWrap::CLAMP_TO_EDGE);
}

// ── demo ─────────────────────────────────────────────────────────────────

int main(int argc, char** argv)
{
    const int maxFrames = (argc > 1) ? atoi(argv[1]) : 0;
    const float kTerrainHalf = 120.f;

    DemoApp app;
    if (!app.Create("coregl - water")) return 1;
    printf("controls: WASD move | Q/E down/up | left mouse look | LSHIFT fast | V debug views | "
           "F10 gif\n");

    SceneRenderer renderer;
    if (!renderer.init())
    {
        fprintf(stderr, "renderer init failed\n");
        app.Destroy();
        return 1;
    }
    renderer.set_light_dir(Vec3(0.4f, -0.7f, 0.3f));
    renderer.set_clear_color(0.55f, 0.70f, 0.85f);
    renderer.enable_shadows(4, 2048, 250.f); // CSM composes with the water views

    // the tree; meshes/materials are owned by the scene, textures by the
    // AssetManager — no naked new, no manual frees
    Scene scene;
    assets::AssetManager& assets = assets::AssetManager::instance();

    Mesh* terrainMesh = scene.create_mesh();
    buildTerrain(*terrainMesh, 160, kTerrainHalf);
    Mesh* cubeMesh = scene.create_mesh();
    demoBuildCube(*cubeMesh);
    gl::Texture* terrainTex = assets.createTexture("terrain");
    buildTerrainTexture(*terrainTex, 512, kTerrainHalf);

    Material* terrainMat = scene.create_material();
    terrainMat->diffuse = terrainTex;
    Material* cubeMat = scene.create_material(0.85f, 0.35f, 0.25f);

    MeshInstance* terrain = scene.root().create_child<MeshInstance>("terrain");
    terrain->set_mesh(terrainMesh);
    terrain->set_material(terrainMat);

    // landmark cubes on high ground
    int placed = 0;
    for (int i = 0; i < 40 && placed < 14; ++i)
    {
        float a = (float)i * 2.399963f; // golden angle scatter
        float r = 18.f + 5.3f * (i % 7) + 0.35f * i;
        float x = r * cosf(a), z = r * sinf(a);
        float h = terrainHeight(x, z);
        if (h < 1.5f) continue; // keep them out of the water
        MeshInstance* cube = scene.root().create_child<MeshInstance>("cube");
        cube->set_mesh(cubeMesh);
        cube->set_material(cubeMat);
        cube->set_position(x, h - 0.2f, z);
        cube->set_scale(2.f + (placed % 3));
        ++placed;
    }

    // the water surface: just another node in the tree
    WaterNode* water = scene.root().create_child<WaterNode>("water");
    water->set_size(kTerrainHalf);
    water->set_position(0.f, 0.f, 0.f);

    Camera3D* camera = scene.root().create_child<Camera3D>("fly");
    camera->set_perspective(55.f, 0.1f, 600.f);
    camera->set_position(0.f, 10.f, 70.f);

    scene.set_active_camera(camera);
    scene.ready();

    // free-fly state drives the camera NODE; angles in radians
    float camYaw = 0.f, camPitch = -0.18f;
    bool looking = false;
    bool debugViews = true; // show the reflection/refraction textures (V toggles)
    renderer.set_debug_views(debugViews);
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

    scene.release_gpu(); // water targets + meshes, in one call
    assets.release();    // textures
    renderer.release();
    app.Destroy();
    return 0;
}
