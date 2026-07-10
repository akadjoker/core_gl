// TiledTerrainNode demo: a flat tile-atlas terrain (think classic RTS/RPG
// ground). The atlas is generated procedurally (4x4 distinct tiles) and the
// tilemap draws roads and fields; each patch of tiles is one surface, so
// off-screen patches cull automatically.
//
// Controls: WASD/QE + mouse, LSHIFT fast, F10 gif, ESC quit.

#include "demo_app.hpp"
#include "demo_fly.hpp"
#include "demo_perf.hpp"
#include <scene/Scene.hpp>
#include <scene/SceneRenderer.hpp>
#include <scene/Material.hpp>
#include <scene/TiledTerrainNode.hpp>
#include <scene/Pixmap.hpp>
#include <scene/AssetManager.hpp>
#include <vector>

// 4x4 tile atlas: grass, dirt, road, water-ish, each with a border so tiles
// read as tiles
static void buildAtlas(gl::Texture& tex, int size)
{
    scene::Pixmap pix(size, size, 4);
    const int tile = size / 4;
    const gl::u8 base[16][3] = {
        {96, 150, 74},  {120, 100, 70}, {130, 130, 130}, {70, 110, 160},
        {86, 140, 66},  {140, 120, 84}, {150, 150, 150}, {60, 100, 150},
        {106, 160, 84}, {110, 90, 60},  {110, 110, 110}, {80, 120, 170},
        {96, 150, 74},  {126, 106, 76}, {140, 140, 140}, {70, 110, 160},
    };
    for (int j = 0; j < size; ++j)
    {
        for (int i = 0; i < size; ++i)
        {
            int t = (j / tile) * 4 + (i / tile);
            int lx = i % tile, lz = j % tile;
            gl::u8 r = base[t][0], g = base[t][1], b = base[t][2];
            // simple per-tile variation + darker border
            int n = ((i * 31 + j * 17) % 13) - 6;
            r = (gl::u8)Clamp(r + n, 0, 255);
            g = (gl::u8)Clamp(g + n, 0, 255);
            b = (gl::u8)Clamp(b + n, 0, 255);
            if (lx == 0 || lz == 0 || lx == tile - 1 || lz == tile - 1)
            {
                r = (gl::u8)(r * 3 / 4);
                g = (gl::u8)(g * 3 / 4);
                b = (gl::u8)(b * 3 / 4);
            }
            pix.set_pixel((gl::u32)i, (gl::u32)j, r, g, b, 255);
        }
    }
    tex.Load2D(pix.pixels, size, size, gl::TextureFormat::RGBA8);
    tex.SetFilter(gl::TextureFilter::LINEAR, gl::TextureFilter::LINEAR);
    tex.SetWrap(gl::TextureWrap::CLAMP_TO_EDGE, gl::TextureWrap::CLAMP_TO_EDGE);
}

int main(int argc, char** argv)
{
    const int maxFrames = (argc > 1) ? atoi(argv[1]) : 0;

    DemoApp app;
    if (!app.Create("coregl - tiled terrain")) return 1;
    printf("controls: WASD/QE + mouse | LSHIFT fast | F10 gif\n");

    SceneRenderer renderer;
    if (!renderer.init())
    {
        fprintf(stderr, "renderer init failed\n");
        app.Destroy();
        return 1;
    }
    renderer.set_light_dir(Vec3(0.4f, -0.8f, -0.3f));
    renderer.set_sky_enabled(true);

    Scene scene;
    assets::AssetManager& assets = assets::AssetManager::instance();

    gl::Texture* atlas = assets.createTexture("tile_atlas");
    buildAtlas(*atlas, 512);

    // 64x64 tilemap: grass with dirt fields and a road cross
    const int W = 64, H = 64;
    std::vector<u8> map((size_t)W * H, 0);
    for (int z = 0; z < H; ++z)
    {
        for (int x = 0; x < W; ++x)
        {
            u8 t = 0;                                            // grass
            if ((x / 7 + z / 5) % 4 == 1) t = 1;                 // dirt fields
            if ((x > 20 && x < 44) && (z > 26 && z < 34)) t = 3; // pond
            if (x == 31 || x == 32 || z == 15 || z == 16) t = 2; // roads
            map[(size_t)z * W + x] = t;
        }
    }

    // 4 tiles per patch -> 16x16 patches, each its own cullable surface
    TiledTerrainNode* terrain =
        scene.root().create_child<TiledTerrainNode>(4, 16.f, 4, (u8)0, "tiled");
    terrain->load_tilemap(W, H, map.data());
    Material* mat = scene.create_material();
    mat->diffuse = atlas;
    terrain->set_material(mat);

    Camera3D* camera = scene.root().create_child<Camera3D>("fly");
    camera->set_perspective(55.f, 0.1f, 600.f);
    camera->set_position(128.f, 24.f, 190.f);

    scene.set_active_camera(camera);
    scene.ready();

    FlyCam fly;
    fly.speed = 24.f;
    gl::u64 lastTicks = SDL_GetPerformanceCounter();
    const gl::u64 freq = SDL_GetPerformanceFrequency();

    PerfPrinter perf;
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
        perf.tick(frame, renderer.last_item_count(), dt);
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
