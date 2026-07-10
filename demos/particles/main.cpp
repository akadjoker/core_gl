// ParticleSystemNode demo: a fire+smoke emitter and a fountain of sparks
// over the block terrain, to show off additive vs. alpha blending, shape
// emitters, affectors (gravity, turbulence) and burst/continuous modes.
//
// Controls: WASD/QE + mouse, LSHIFT fast, F10 gif, ESC quit.

#include "demo_app.hpp"
#include "demo_fly.hpp"
#include "demo_perf.hpp"
#include <scene/Scene.hpp>
#include <scene/SceneRenderer.hpp>
#include <scene/Material.hpp>
#include <scene/TerrainNode.hpp>
#include <scene/ParticleSystemNode.hpp>
#include <scene/DecalSystemNode.hpp>
#include <scene/GrassSystemNode.hpp>
#include <scene/RibbonTrailNode.hpp>
#include <scene/AssetManager.hpp>
#include <scene/Pixmap.hpp>

// soft round dot: alpha falls off from center — the standard particle glyph
static void buildDotTexture(gl::Texture& tex, int size)
{
    scene::Pixmap pix(size, size, 4);
    float c = size * 0.5f;
    for (int y = 0; y < size; ++y)
    {
        for (int x = 0; x < size; ++x)
        {
            float d = sqrtf((x - c) * (x - c) + (y - c) * (y - c)) / c;
            float a = 1.f - d;
            if (a < 0.f) a = 0.f;
            a = a * a;
            pix.set_pixel((gl::u32)x, (gl::u32)y, 255, 255, 255, (gl::u8)(a * 255.f));
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
    if (!app.Create("coregl - particles")) return 1;
    printf("controls: WASD/QE + mouse | LSHIFT fast | F10 gif\n");

    SceneRenderer renderer;
    if (!renderer.init())
    {
        fprintf(stderr, "renderer init failed\n");
        app.Destroy();
        return 1;
    }
    renderer.set_light_dir(Vec3(0.f, 0.55f, 0.84f));
    renderer.set_sky_enabled(true);

    Scene scene;
    assets::AssetManager& assets = assets::AssetManager::instance();

      

    gl::Texture* particle = assets.loadTexture("particles", "assets/textures/light.jpg");

    gl::Texture* dot = assets.loadTexture("GRASS", "assets/textures/grass1.png");
    //assets.createTexture("particle_dot");
    //buildDotTexture(*dot, 64);

    // simple flat ground so the emitters have something to sit on
    Mesh* groundMesh = scene.create_mesh();
    {
        MeshVertex v[4];
        const float h = 60.f;
        const float pos[4][3] = {{-h, 0, -h}, {h, 0, -h}, {h, 0, h}, {-h, 0, h}};
        for (int i = 0; i < 4; ++i)
        {
            v[i].position = Vec3(pos[i][0], pos[i][1], pos[i][2]);
            v[i].normal = Vec3(0.f, 1.f, 0.f);
            v[i].tangent = Vec4(1.f, 0.f, 0.f, 1.f);
            v[i].uv = Vec2((float)(i == 1 || i == 2), (float)(i >= 2));
        }
        const u16 idx[6] = {0, 2, 1, 0, 3, 2};
        groundMesh->set_data(v, 4, idx, 6);
        groundMesh->upload();
    }
    Material* groundMat = scene.create_material(0.30f, 0.32f, 0.30f);
    MeshInstance* ground = scene.root().create_child<MeshInstance>("ground");
    ground->set_mesh(groundMesh);
    ground->set_material(groundMat);

    // ── grass patch: wind-swaying alpha-cutout blades, static once built ──
    GrassSystemNode* grass = scene.root().create_child<GrassSystemNode>(
        "grass", 2000, GrassSystemNode::GrassType::Cross);
    grass->texture = dot; // soft round dot reused as a stand-in blade mask
    grass->set_cutout(0.15f);
    grass->set_wind(Vec3(1.f, 0.f, 0.3f), 0.15f, 1.8f);
    grass->fillArea(Vec3(0.f, 0.f, 8.f), 40.f, 20.f, 800);
    grass->build();

    // ── fire + smoke: cone emitter, additive glow, gravity+drag+turbulence ──
    ParticleSystemNode* fire = scene.root().create_child<ParticleSystemNode>("fire", 400);
    fire->texture = particle;
    fire->blend = ParticleBlendMode::Additive;
    fire->set_position(-15.f, 0.f, 0.f);
    fire->setContinuous(120.f)
        ->setShapeCircle(1.2f)
        ->setEmissionDirection(Vec3(0.f, 1.f, 0.f))
        ->setSpreadAngle(12.f)
        ->setSpeed(2.5f, 4.5f)
        ->setLifetime(0.6f, 1.1f)
        ->setSize(Vec2(1.4f, 1.4f), Vec2(0.2f, 0.2f))
        ->setColor(Vec4(1.0f, 0.65f, 0.15f, 0.9f), Vec4(0.5f, 0.1f, 0.05f, 0.0f));
    fire->addTurbulence(0.6f, 1.5f);
    fire->addDrag(0.3f);

    // ── fountain of sparks: point emitter, gravity pulls them back down ──
    ParticleSystemNode* fountain = scene.root().create_child<ParticleSystemNode>("fountain", 600);
    fountain->texture = particle;
    fountain->blend = ParticleBlendMode::Additive;
    fountain->set_position(15.f, 0.f, 0.f);
    fountain->setContinuous(200.f)
        ->setShapePoint()
        ->setEmissionDirection(Vec3(0.f, 1.f, 0.f))
        ->setSpreadAngle(18.f)
        ->setSpeed(9.f, 13.f)
        ->setLifetime(1.4f, 1.9f)
        ->setSize(Vec2(0.35f, 0.35f), Vec2(0.1f, 0.1f))
        ->setColor(Vec4(0.6f, 0.85f, 1.0f, 1.0f), Vec4(0.3f, 0.5f, 1.0f, 0.0f));
    fountain->setGravity(Vec3(0.f, -9.8f, 0.f));

    // ── smoke puff: alpha-blended burst, box emitter, slow rise ──
    ParticleSystemNode* smoke = scene.root().create_child<ParticleSystemNode>("smoke", 200);
    smoke->texture = particle;
    smoke->blend = ParticleBlendMode::Alpha;
    smoke->set_position(0.f, 0.f, -15.f);
    smoke->setBurst(30, 1.2f)
        ->setShapeBox(Vec3(2.f, 0.5f, 2.f))
        ->setEmissionDirection(Vec3(0.f, 1.f, 0.f))
        ->setSpreadAngle(8.f)
        ->setSpeed(1.5f, 2.5f)
        ->setLifetime(2.5f, 3.5f)
        ->setSize(Vec2(0.8f, 0.8f), Vec2(3.5f, 3.5f))
        ->setColor(Vec4(0.6f, 0.6f, 0.6f, 0.35f), Vec4(0.6f, 0.6f, 0.6f, 0.0f));
    // ── ribbon trail: follows a node that spins in a figure-8 ──
    Node3D* orbiter = scene.root().create_child<Node3D>("orbiter");
    orbiter->set_position(0.f, 2.5f, -5.f);

    RibbonTrailNode* trail = scene.root().create_child<RibbonTrailNode>("trail", 1, 80);
    trail->texture = particle;
    trail->blend = ParticleBlendMode::Additive;
    trail->setTrailLength(2.5f);
    trail->addChain(orbiter, Vec4(1.0f, 0.45f, 0.1f, 0.9f),
                    Vec4(0.8f, 0.2f, 0.05f, 0.0f), 0.40f, 0.04f);
    // ── scorch decals on the ground, normal-oriented (not billboarded) ──
    DecalSystemNode* decals = scene.root().create_child<DecalSystemNode>("decals", 64);
    decals->texture = dot;
    decals->blend = ParticleBlendMode::Alpha;
    decals->set_default_lifetime(-1.f); // permanent marks
    for (int i = 0; i < 6; ++i)
    {
        float a = (float)i / 6.f * 6.28318f;
        Vec3 pos(cosf(a) * 8.f, 0.02f, sinf(a) * 8.f - 15.f); // ring under the smoke puff
        decals->add(pos, Vec3(0.f, 1.f, 0.f), Vec2(2.5f, 2.5f), Vec4(0.05f, 0.05f, 0.05f, 0.8f));
    }

    Camera3D* camera = scene.root().create_child<Camera3D>("fly");
    camera->set_perspective(55.f, 0.1f, 400.f);
    camera->set_position(0.f, 6.f, 35.f);

    scene.set_active_camera(camera);
    scene.ready();

    FlyCam fly;
    fly.speed = 18.f;
    gl::u64 lastTicks = SDL_GetPerformanceCounter();
    const gl::u64 freq = SDL_GetPerformanceFrequency();

    PerfPrinter perf;
    int frame = 0;
    bool running = true;
    float timeOfDay = getenv("COREGL_TOD") ? (float)atof(getenv("COREGL_TOD")) : 0.9f;
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
        const Uint8* keys = SDL_GetKeyboardState(nullptr);

        if (keys[SDL_SCANCODE_T]) timeOfDay += dt * 0.4f;
        if (keys[SDL_SCANCODE_G]) timeOfDay -= dt * 0.4f;
        Vec3 sunDir = Vec3(cosf(timeOfDay) * 0.8f, sinf(timeOfDay), 0.35f).normalized();
        renderer.set_light_dir(sunDir * -1.f);

        fly.apply(camera, dt);
        scene.update(dt);

        // animate the ribbon-trail orbiter: figure-8 over time
        {
            float t = (float)frame * 0.03f;
            float x = sinf(t) * 10.f;
            float z = cosf(t * 0.5f) * 6.f - 5.f;
            float y = 2.5f + sinf(t * 2.f) * 1.2f;
            orbiter->set_position(x, y, z);
        }

        int w, h;
        app.DrawableSize(&w, &h);
        renderer.render(scene, w, h);
        perf.tick(frame, renderer.last_item_count(), dt);
        app.EndFrame();

        ++frame;
        if (maxFrames > 0 && frame >= maxFrames) running = false;
    }

    printf("frames: %d | items/frame: %d | fire %d | fountain %d | smoke %d\n", frame,
           renderer.last_item_count(), fire->active_count(), fountain->active_count(),
           smoke->active_count());
    scene.release_gpu();
    assets.release();
    renderer.release();
    app.Destroy();
    return 0;
}
