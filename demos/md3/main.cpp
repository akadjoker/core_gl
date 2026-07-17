// MD2/MD3 vertex (morph) animation demo. Three parts:
//  - a simple single-part MD2 (pknight.md2) to smoke-test the basic morph
//    blend + Mesh::update_vertices() path in isolation.
//  - the classic Quake3 player rig assembled from 3 separate MD3 parts via
//    MD3 tags: lower.md3 (root) -> tag_torso -> upper.md3 -> tag_head ->
//    head.md3. Validates TagAttachment end-to-end: if the tags are wrong
//    the pieces won't line up at the neck/waist seams. Clips are read
//    straight out of the model's own animation.cfg (LEGS_WALK/LEGS_IDLE
//    for the legs, TORSO_POINT for the torso) instead of guessed frame
//    ranges, so what plays is an actual named Q3 animation.
//  - a Beretta held in the player's tag_weapon, itself broken into parts
//    (main/slide/trigger/safety/cock/sliderel/clip) glued together via
//    beretta_hold.md3's own 11 tags — a second, independent test of the
//    same tag-attachment machinery on a non-player rig.
//
// Controls: WASD/QE + mouse | 1/2 crossfade the legs clip | F10 gif | ESC

#include "demo_app.hpp"
#include "demo_fly.hpp"
#include <scene/AssetManager.hpp>
#include <scene/ByteArray.hpp>
#include <scene/Filesystem.hpp>
#include <scene/Material.hpp>
#include <scene/MorphMeshInstance.hpp>
#include <scene/Scene.hpp>
#include <scene/SceneRenderer.hpp>
#include <scene/TagAttachment.hpp>
#include <cstdio>
#include <sstream>
#include <string>
#include <unordered_map>

// Minimal Quake3 animation.cfg reader — not the full semantics (no
// legs/torso frame-offset reconciliation, no flip/hit columns), just
// enough to pull a named clip's (start, count, fps, loop) instead of
// guessing frame ranges by hand. Columns per the file's own header
// comment: "Animation  Start  Count  FPS  Loop  Flip  Upper  Lower  Hit".
struct CfgClip
{
    int start = 0, count = 1;
    float fps = 10.f;
    bool loop = true;
};

// Goes through fs::Filesystem (like every mesh/texture load elsewhere in
// this codebase) instead of raw fopen() — fopen() resolves relative paths
// against the process's current working directory, which varies by how
// the binary gets launched (terminal vs. IDE run button vs. debugger), so
// a relative path that works one way silently fails another. Filesystem's
// configured search paths don't have that problem.
static std::unordered_map<std::string, CfgClip> parse_animation_cfg(const char* path)
{
    std::unordered_map<std::string, CfgClip> out;
    scene::ByteArray bytes;
    if (!fs::getFilesystem().readFile(path, bytes))
    {
        fprintf(stderr, "animation.cfg: could not open %s\n", path);
        return out;
    }
    std::istringstream file(std::string((const char*)bytes.data(), bytes.size()));
    std::string line;
    while (std::getline(file, line))
    {
        std::istringstream ss(line);
        std::string name;
        if (!(ss >> name) || name.empty() || name[0] == '/' || name[0] == '#') continue;

        CfgClip c;
        int loopField = 0;
        if (!(ss >> c.start >> c.count >> c.fps >> loopField)) continue;
        c.loop = loopField != 0;
        if (c.fps <= 0.f) c.fps = 10.f;
        out[name] = c;
    }
    return out;
}

// clamps a clip range into whatever frame count the model actually has —
// protects against animation.cfg entries that don't apply cleanly to this
// particular exported .md3 (e.g. the classic Q3 legs/torso frame-offset
// quirk, which this reader doesn't reconcile).
static void add_safe_clip(MorphMeshInstance* inst, const char* name, const CfgClip& c, int frameCount)
{
    if (frameCount <= 0) return;
    int first = c.start < 0 ? 0 : (c.start >= frameCount ? frameCount - 1 : c.start);
    int last = c.start + c.count - 1;
    last = last < 0 ? 0 : (last >= frameCount ? frameCount - 1 : last);
    if (last < first) last = first;
    inst->add_clip(name, first, last, c.fps, c.loop);
}

int main(int argc, char** argv)
{
    const int maxFrames = (argc > 1) ? atoi(argv[1]) : 0;

    DemoApp app;
    if (!app.Create("coregl - md2/md3 morph animation")) return 1;
    printf("controls: WASD/QE + mouse | 1/2 crossfade legs clip | F10 gif | ESC\n");

    SceneRenderer renderer;
    if (!renderer.init())
    {
        fprintf(stderr, "renderer init failed\n");
        app.Destroy();
        return 1;
    }
    renderer.set_light_dir(Vec3(0.35f, -0.75f, 0.45f));
    renderer.set_sky_enabled(true);
    renderer.enable_shadows();

    Scene scene;
    assets::AssetManager& assets = assets::AssetManager::instance();

    auto cfg = parse_animation_cfg("assets/models/md3/orion/animation.cfg");
    printf("animation.cfg: %d clips parsed\n", (int)cfg.size());

    // ── MD2 smoke test: a single simple morph mesh, off to the side ──
    {
        MorphKeyframes anim;
        std::vector<Material*> mats;
        Mesh* mesh = assets.load_md2_mesh("pknight", "assets/models/md2/pknight.md2", anim, mats);
        if (mesh)
        {
            int frameCount = anim.frame_count();
            MorphMeshInstance* knight = scene.root().create_child<MorphMeshInstance>("pknight");
            knight->set_anim_data(mesh, std::move(anim), {});
            knight->set_materials(mats);
            knight->set_position(-25.f, 5.f, 0.f);
            knight->add_clip("spin", 0, frameCount - 1, 8.f, true);
            knight->play("spin");
            printf("pknight.md2: %d frames\n", frameCount);
        }
    }

    // ── MD3 rig: lower -> tag_torso -> upper -> tag_head -> head ──
    MorphMeshInstance* lower = nullptr;
    MorphMeshInstance* upper = nullptr;
    {
        MorphKeyframes anim;
        MorphTags tags;
        std::vector<Material*> mats;
        Mesh* mesh = assets.load_md3_mesh("orion_lower", "assets/models/md3/orion/lower.md3", anim,
                                          mats, &tags);
        if (mesh)
        {
            int frameCount = anim.frame_count();
            lower = scene.root().create_child<MorphMeshInstance>("lower");
            lower->set_anim_data(mesh, std::move(anim), std::move(tags));
            lower->set_materials(mats);
            lower->set_position(0.f, 0.f, 0.f);
            add_safe_clip(lower, "walk", cfg["LEGS_WALK"], frameCount);
            add_safe_clip(lower, "idle", cfg["LEGS_IDLE"], frameCount);
            lower->play("walk");
            printf("lower.md3: %d frames, %d tags\n", frameCount, (int)mesh->surfaces().size());
        }
    }

    if (lower)
    {
        MorphKeyframes anim;
        MorphTags tags;
        std::vector<Material*> mats;
        Mesh* mesh = assets.load_md3_mesh("orion_upper", "assets/models/md3/orion/upper.md3", anim,
                                          mats, &tags);
        if (mesh)
        {
            TagAttachment* torsoAttach = lower->create_child<TagAttachment>("torso_attach");
            if (!torsoAttach->attach(lower, "tag_torso"))
                fprintf(stderr, "md3: lower.md3 has no tag_torso\n");

            upper = torsoAttach->create_child<MorphMeshInstance>("upper");
            int frameCount = anim.frame_count();
            upper->set_anim_data(mesh, std::move(anim), std::move(tags));
            upper->set_materials(mats);
            add_safe_clip(upper, "point", cfg["TORSO_POINT"], frameCount);
            upper->play("point");

            // ── head, attached to the upper's tag_head ──
            MorphKeyframes headAnim;
            std::vector<Material*> headMats;
            Mesh* headMesh = assets.load_md3_mesh("orion_head", "assets/models/md3/orion/head.md3",
                                                  headAnim, headMats);
            if (headMesh)
            {
                TagAttachment* headAttach = upper->create_child<TagAttachment>("head_attach");
                if (!headAttach->attach(upper, "tag_head"))
                    fprintf(stderr, "md3: upper.md3 has no tag_head\n");

                MorphMeshInstance* head = headAttach->create_child<MorphMeshInstance>("head");
                head->set_anim_data(headMesh, std::move(headAnim), {});
                head->set_materials(headMats);

                
            }
        }
    }

 
    // MorphMeshInstance* holder;
    //     const char* kDir = "assets/models/md3/beretta/";
    //     // for (const char* part : {"beretta_1.md3", "beretta_2.md3"})
    //     {

            
    //         // Animation	Start	Count	FPS		Loop

    //     // WEAPON_DRAW				0	54	32	0
    //     // WEAPON_IDLE				44	1	10	1
    //     // WEAPON_IDLE_EMPTY		75	1	10	1
    //     // WEAPON_FIRE				54	10	36	0
    //     // WEAPON_FIRE_LAST		65	10	36	0
    //     // WEAPON_RELOAD			75	48	28	0


    //         MorphKeyframes anim;
    //         std::vector<Material*> mats;
    //         MorphTags holderTags;
    //         std::string path = std::string(kDir) + "beretta_hold.md3";
    //         Mesh* mesh = assets.load_md3_mesh("holder","assets/models/md3/beretta/beretta_hold.md3", anim, mats, &holderTags);

    //         mats[0]->diffuse = assets.loadTexture("holder_diffuse", "assets/models/md3/hand_swat.png");
    //         mats[1]->diffuse = assets.loadTexture("holder_diffuse", "assets/models/md3/hand_swat.png");

    //         int holderFrameCount = anim.frame_count(); // capture BEFORE the move below empties anim

    //         holder = scene.root().create_child<MorphMeshInstance>("holder");
    //         holder->set_anim_data(mesh, std::move(anim), std::move(holderTags));
    //         holder->set_materials(mats);
    
    //         auto cfg = parse_animation_cfg("assets/models/md3/beretta/animation.cfg");
    //        printf("berreta animation.cfg: %d clips parsed\n", (int)cfg.size());



    //         add_safe_clip(holder, "draw", cfg["WEAPON_DRAW"], holderFrameCount);
    //         add_safe_clip(holder, "idle", cfg["WEAPON_IDLE"], holderFrameCount);
    //         add_safe_clip(holder, "idle_empty", cfg["WEAPON_IDLE_EMPTY"], holderFrameCount);
    //         add_safe_clip(holder, "fire", cfg["WEAPON_FIRE"], holderFrameCount);
    //         add_safe_clip(holder, "fire_last", cfg["WEAPON_FIRE_LAST"], holderFrameCount);
    //         add_safe_clip(holder, "reload", cfg["WEAPON_RELOAD"], holderFrameCount);
    //         holder->play("draw");

    //         {
    //             MorphKeyframes anim;
    //             std::vector<Material*> mats;

    //             Mesh* mesh = assets.load_md3_mesh("beretta_main", "assets/models/md3/beretta/beretta_view_main.md3", anim, mats);
    //             if (mesh)
    //             {
    //                 TagAttachment* mainAttach = holder->create_child<TagAttachment>("main_attach");
    //                 if (!mainAttach->attach(holder, "tag_main"))
    //                     fprintf(stderr, "md3: holder has no tag_main\n");

    //                 mats[0]->diffuse = assets.loadTexture("main_diffuse", "assets/models/md3/beretta/beretta.jpeg");

    //                 MorphMeshInstance* main = mainAttach->create_child<MorphMeshInstance>("main");
    //                 main->set_anim_data(mesh, std::move(anim), {});
    //                 main->set_materials(mats);

    //             }
    //         }
    //         {
    //             MorphKeyframes anim;
    //             std::vector<Material*> mats;
    //             Mesh* mesh = assets.load_md3_mesh("beretta_clip", "assets/models/md3/beretta/beretta_view_clip.md3", anim, mats);
    //             if (mesh)
    //             {
    //                 TagAttachment* clipAttach = holder->create_child<TagAttachment>("clip_attach");
    //                 if (!clipAttach->attach(holder, "tag_clip"))
    //                     fprintf(stderr, "md3: holder has no tag_clip\n");


    //                 mats[0]->diffuse = assets.loadTexture("main_diffuse", "assets/models/md3/beretta/beretta.jpeg");
    //                 MorphMeshInstance* clip = clipAttach->create_child<MorphMeshInstance>("clip");
    //                 clip->set_anim_data(mesh, std::move(anim), {});
    //                 clip->set_materials(mats);
    //                 clip->set_scale(0.15f);
    //             }
    //         }
    //         {
    //             MorphKeyframes anim;
    //             std::vector<Material*> mats;
    //             Mesh* mesh = assets.load_md3_mesh("beretta_cock", "assets/models/md3/beretta/beretta_view_cock.md3", anim, mats);
    //             if (mesh)
    //             {
    //                 TagAttachment* cockAttach = holder->create_child<TagAttachment>("cock_attach");
    //                 if (!cockAttach->attach(holder, "tag_cock"))
    //                     fprintf(stderr, "md3: holder has no tag_cock\n");

    //                 mats[0]->diffuse = assets.loadTexture("main_diffuse", "assets/models/md3/beretta/beretta.jpeg");
    //                 MorphMeshInstance* cock = cockAttach->create_child<MorphMeshInstance>("cock");
    //                 cock->set_anim_data(mesh, std::move(anim), {});
    //                 cock->set_materials(mats);
    //             }
    //         }
    //         {
    //             MorphKeyframes anim;
    //             std::vector<Material*> mats;
    //             Mesh* mesh = assets.load_md3_mesh("beretta_slide", "assets/models/md3/beretta/beretta_view_slide.md3", anim, mats);
    //             if (mesh)
    //             {
    //                 TagAttachment* slideAttach = holder->create_child<TagAttachment>("slide_attach");
    //                 if (!slideAttach->attach(holder, "tag_slide"))
    //                     fprintf(stderr, "md3: holder has no tag_slide\n");

    //                 mats[0]->diffuse = assets.loadTexture("main_diffuse", "assets/models/md3/beretta/beretta.jpeg");
    //                 MorphMeshInstance* slide = slideAttach->create_child<MorphMeshInstance>("slide");
    //                 slide->set_anim_data(mesh, std::move(anim), {});
    //                 slide->set_materials(mats);
    //                 slide->set_scale(0.15f);
    //             }
    //         }


    //     }
 

    Camera3D* camera = scene.root().create_child<Camera3D>("fly");
    camera->set_perspective(50.f, 0.1f, 300.f);
    camera->set_position(0.f, 40.f, 90.f);

    scene.set_active_camera(camera);
    scene.ready();

    FlyCam fly;
    fly.speed = 40.f;
    gl::u64 lastTicks = SDL_GetPerformanceCounter();
    const gl::u64 freq = SDL_GetPerformanceFrequency();

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
                if (lower && ev.key.keysym.sym == SDLK_1) lower->play("walk", 0.3f);
                if (lower && ev.key.keysym.sym == SDLK_2) lower->play("idle", 0.3f);
              //  if (holder && ev.key.keysym.sym == SDLK_3) holder->play("reload", 0.3f);
              //  if (holder && ev.key.keysym.sym == SDLK_4) holder->play("fire", 0.3f);
              //  if (holder && ev.key.keysym.sym == SDLK_5) holder->play("draw", 0.3f);
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
