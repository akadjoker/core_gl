// Fresnel demo (WIP): building toward Ogre's classic "Fresnel" sample —
// reflective/refractive water over the sunken Roman bath ruins. Loads the
// three static pieces (RomanBathUpper, RomanBathLower, Columns), converted
// from the original Ogre .mesh files via mesh2h3d/ (see assets/fresneldemo/,
// converted from tmp/Simple's Fresnel.h asset list), with diffuse textures
// looked up by material name (no .material script parsing, no lightmaps —
// see kUpperLowerLooks/kColumnsLooks below). Still missing: water/Fresnel
// reflection, the BlueTile caustic animation, fish.mesh (skeletal, out of
// scope for now).
//
// Controls: WASD/QE + mouse, LSHIFT fast, F10 gif, ESC quit.

#include "demo_app.hpp"
#include "demo_fly.hpp"
#include <scene/Scene.hpp>
#include <scene/SceneRenderer.hpp>
#include <scene/Material.hpp>
#include <scene/MeshInstance.hpp>
#include <scene/MeshLoader.hpp>
#include <scene/LightNode.hpp>
#include <scene/Mesh.hpp>
#include <scene/WaterNode.hpp>
#include <scene/AssetManager.hpp>
#include <scene/LensFlareNode.hpp>
#include <chrono>

struct MaterialLook
{
    const char* materialName;
    const char* diffuseTexture; // nullptr = no diffuse texture (flat grey)
};

// read straight off assets/fresneldemo/RomanBath.material by eye — no
// script parsing, just the diffuse texture per material name. Lightmaps are
// a separate follow-up, not here.
static const MaterialLook kUpperLowerLooks[] = {
    {"RomanBath/Default_Grey", nullptr},
    {"RomanBath/PoolSidesHigh", "marble1.jpg"},
    {"RomanBath/PoolSidesLow", "marble1.jpg"},
    {"RomanBath/StoneBlock", "stone1.jpg"},
    {"RomanBath/MainFloor", "cfloor.png"},
    {"RomanBath/OuterWallLow", "wall3.jpg"},
    {"RomanBath/MarbleFloor", "marble2.jpg"},
    {"RomanBath/BlueTile", "bluetile.png"}, // caustic overlay: follow-up
    {"RomanBath/RimSkirting", "skirting.png"},
};
static const MaterialLook kColumnsLooks[] = {
    {"RomanBath/ColumnMarble", "marble3.png"},
    {"RomanBath/ColumnBlock", "stone1.jpg"},
};

struct Piece
{
    const char* file;
    const char* name;
    const MaterialLook* looks;
    int lookCount;
};

static gl::Texture* find_diffuse(assets::AssetManager& assets, const std::string& materialName,
                                 const MaterialLook* looks, int count)
{
    for (int i = 0; i < count; ++i)
        if (materialName == looks[i].materialName)
            return looks[i].diffuseTexture
                       ? assets.loadTexture(looks[i].diffuseTexture,
                                            (std::string("assets/fresneldemo/textures/") +
                                             looks[i].diffuseTexture)
                                                .c_str())
                       : nullptr;
    fprintf(stderr, "fresnel: no material lookup for '%s'\n", materialName.c_str());
    return nullptr;
}

int main(int argc, char** argv)
{
    const int maxFrames = (argc > 1) ? atoi(argv[1]) : 0;

    DemoApp app;
    if (!app.Create("coregl - fresnel (wip)")) return 1;
    printf("controls: WASD/QE + mouse | LSHIFT fast | F10 gif | ESC quit\n");

    SceneRenderer renderer;
    if (!renderer.init())
    {
        fprintf(stderr, "renderer init failed\n");
        app.Destroy();
        return 1;
    }
    renderer.set_light_dir(Vec3(0.4f, -0.8f, 0.3f));
    renderer.set_sky_enabled(true);
    renderer.enable_shadows(4, 1024, 2000.f);
    renderer.enable_post(true, false); // godrays off (indoor, no sky shafts), ssao on
    renderer.set_ssao_params(0.15f, 1.5f);

    Scene scene;
    assets::AssetManager& assets = assets::AssetManager::instance();

    // the three pieces are authored to already fit together at the origin
    // (same as Ogre's Fresnel.h: each is attached to the root node with no
    // offset) — RomanBathLower is the submerged geometry, RomanBathUpper is
    // the above-water structure, Columns are the freestanding pillars.
    const Piece pieces[] = {
        {"assets/fresneldemo/RomanBathUpper.h3d", "upper", kUpperLowerLooks,
         (int)(sizeof(kUpperLowerLooks) / sizeof(kUpperLowerLooks[0]))},
        {"assets/fresneldemo/RomanBathLower.h3d", "lower", kUpperLowerLooks,
         (int)(sizeof(kUpperLowerLooks) / sizeof(kUpperLowerLooks[0]))},
        {"assets/fresneldemo/Columns.h3d", "columns", kColumnsLooks,
         (int)(sizeof(kColumnsLooks) / sizeof(kColumnsLooks[0]))},
    };

    for (const Piece& p : pieces)
    {
        Mesh* mesh = assets.createMesh(p.name);
        std::vector<MeshLoader::MaterialDesc> matDescs;
        if (!MeshLoader::load(p.file, *mesh, &matDescs))
        {
            fprintf(stderr, "failed to load %s\n", p.file);
            continue;
        }
        mesh->upload();
        printf("%s: %u surfaces, %u verts, %u indices\n", p.name,
               (unsigned)mesh->surfaces().size(), mesh->vertex_count(), mesh->index_count());

        // mesh2h3d only carries material names (no .material script parsing)
        // — look up the diffuse texture per name from the table above
        std::vector<Material*> materials;
        materials.reserve(matDescs.size());
        for (const MeshLoader::MaterialDesc& md : matDescs)
        {
            Material* mat = scene.create_material(1.f, 1.f, 1.f);
            mat->diffuse = find_diffuse(assets, md.name, p.looks, p.lookCount);
            materials.push_back(mat);
        }

        MeshInstance* inst = scene.root().create_child<MeshInstance>(p.name);
        inst->set_scale(0.2f); // original Ogre units are 10x bigger than CoreGL's
        inst->set_mesh(mesh);
        inst->set_materials(materials);
    }


    // the water surface: just another node in the tree
    WaterNode* water = scene.root().create_child<WaterNode>("water");
    water->set_size(140.f); // 200x200 world units, centered at the origin
    water->set_position(0.f, 0.f, 0.f);


    Camera3D* camera = scene.root().create_child<Camera3D>("fly");
    camera->set_perspective(60.f, 0.5f, 2000.f);
    camera->set_position(-50.f, 125.f, 760.f); // matches Ogre Fresnel.h's start view

    scene.set_active_camera(camera);
    scene.ready();

    FlyCam fly;
    fly.speed = 150.f;
    gl::u64 lastTicks = SDL_GetPerformanceCounter();
    const gl::u64 freq = SDL_GetPerformanceFrequency();
    float timeOfDay =   0.9f;
    LensFlareNode* flare = scene.root().create_child<LensFlareNode>("sun_flare");

 

    if (getenv("COREGL_GIF_AUTOSTART"))
    {
        int gw, gh;
        app.DrawableSize(&gw, &gh);
        app.gif.Toggle(gw, gh);
    }

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
                    if (ev.key.keysym.sym == SDLK_x)
                {
                    static bool on = true;
                    on = !on;
                    renderer.set_shadows_active(on);
                    printf("shadows: %s\n", on ? "ON" : "off");
                }
                if (ev.key.keysym.sym == SDLK_c)
                {
                    static bool on = false;
                    on = !on;
                    renderer.set_show_cascades(on);
                    printf("cascade debug colors: %s\n", on ? "ON" : "off");
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

        int w, h;
        app.DrawableSize(&w, &h);
        renderer.render(scene, w, h);
        app.EndFrame();

        ++frame;
        if (maxFrames > 0 && frame >= maxFrames) running = false;
    }

    if (getenv("COREGL_GIF_AUTOSTART")) app.gif.Toggle(0, 0);
    printf("frames: %d\n", frame);
    scene.release_gpu();
    assets.release();
    renderer.release();
    app.Destroy();
    return 0;
}
