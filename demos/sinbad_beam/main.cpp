// Sinbad character controller, loosely modeled on Ogre's own
// SinbadCharacterController sample (tmp/Character/include/
// SinbadCharacterController.h): WASD moves the character relative to an
// orbiting third-person camera, the body turns smoothly to face its goal
// direction, and layered Idle/Run animations (lower body + upper body)
// crossfade in and out. A sword rides on the back (bone attachment, no
// offset animation) and can be drawn/sheathed with E; a glowing ribbon
// trail ("beam") always follows the sword wherever it's parented.
//
// Controls: WASD move | mouse-drag orbit | wheel zoom | E draw/sheathe |
// F9 stats | F10 gif | ESC

#include "demo_app.hpp"

#include <scene/Scene.hpp>
#include <scene/SceneRenderer.hpp>
#include <scene/Camera3D.hpp>
#include <scene/Material.hpp>
#include <scene/SkinnedMesh.hpp>
#include <scene/SkinnedMeshInstance.hpp>
#include <scene/BoneAttachment.hpp>
#include <scene/MeshInstance.hpp>
#include <scene/RibbonTrailNode.hpp>
#include <scene/LensFlareNode.hpp>
#include <scene/AssetManager.hpp>

namespace
{
constexpr float kRunSpeed = 9.f;
constexpr float kTurnSpeed = 8.f; // slerp factor per second

 

// mouse-drag orbit camera around a fixed pivot height above the character
struct OrbitCam
{
    float yaw = 0.f;
    float pitch = 0.35f; // positive = camera above the pivot, looking down
    float distance = 14.f;
    bool dragging = false;

    void handle(const SDL_Event& ev)
    {
        if (ev.type == SDL_MOUSEBUTTONDOWN && ev.button.button == SDL_BUTTON_LEFT) dragging = true;
        if (ev.type == SDL_MOUSEBUTTONUP && ev.button.button == SDL_BUTTON_LEFT) dragging = false;
        if (ev.type == SDL_MOUSEMOTION && dragging)
        {
            yaw -= (float)ev.motion.xrel * 0.006f;
            pitch -= (float)ev.motion.yrel * 0.006f;
            if (pitch > 1.2f) pitch = 1.2f;
            if (pitch < -1.2f) pitch = -1.2f;
        }
        if (ev.type == SDL_MOUSEWHEEL)
        {
            distance -= (float)ev.wheel.y * 1.f;
            if (distance < 4.f) distance = 4.f;
            if (distance > 30.f) distance = 30.f;
        }
    }

    void apply(Camera3D* cam, const Vec3& charPos)
    {
        Vec3 pivot = charPos + Vec3(0.f, 3.f, 0.f); // roughly chest height
        Vec3 offset(sinf(yaw) * cosf(pitch) * distance, sinf(pitch) * distance,
                   cosf(yaw) * cosf(pitch) * distance);
        cam->set_position(pivot + offset);
        cam->look_at(pivot);
    }
};
} // namespace

int main(int argc, char** argv)
{
    const int maxFrames = (argc > 1) ? atoi(argv[1]) : 0;

    DemoApp app;
    if (!app.Create("coregl - sinbad beam")) return 1;
    printf("controls: WASD move | drag orbit | wheel zoom | E draw/sheathe sword | "
          "T/G time of day | F9 stats | F10 gif\n");

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
    // renderer.enable_post(true, true);  
    // renderer.set_ssao_params(0.15f, 1.5f);
    if (getenv("COREGL_STATS")) renderer.set_show_stats(true);

    Scene scene;
    assets::AssetManager& assets = assets::AssetManager::instance();


        assets.set_flip_on_lood(true); 

    gl::Texture* bodyTex = assets.loadTexture("sinbad_body", "assets/models/sinbad/sinbad_body.tga");
    gl::Texture* clothTex = assets.loadTexture("sinbad_clothes", "assets/models/sinbad/sinbad_clothes.tga");
    gl::Texture* swordTex = assets.loadTexture("sword", "assets/models/sinbad/sinbad_sword.tga");


    SkinnedMesh* sinbadRes = assets.loadSkinnedMesh("sinbad", "assets/models/sinbad/sinbad.h3d");
    if (!sinbadRes)
    {
        fprintf(stderr, "sinbad load failed\n");
        return 1;
    }
    const char* anims[] = {"IdleBase",  "IdleTop",         "RunBase", "RunTop",
                           "DrawSwords", "SliceVertical",   "SliceHorizontal"};
    for (const char* a : anims)
    {
        char path[128];
        snprintf(path, sizeof(path), "assets/models/sinbad/sinbad_%s.anim", a);
        assets.loadAnimation("sinbad", path);
    }
    const AnimationClip* drawClip = sinbadRes->find_clip("DrawSwords");
    const float drawSwordsLen = drawClip ? drawClip->duration() : 0.33f;

    // ground
    Mesh* groundMesh = scene.create_mesh();
    {
        MeshVertex v[4];
        const float h = 40.f;
        const float pos[4][3] = {{-h, 0, -h}, {h, 0, -h}, {h, 0, h}, {-h, 0, h}};
        for (int i = 0; i < 4; ++i)
        {
            v[i].position = Vec3(pos[i][0], pos[i][1], pos[i][2]);
            v[i].normal = Vec3(0.f, 1.f, 0.f);
            v[i].tangent = Vec4(1.f, 0.f, 0.f, 1.f);
            v[i].uv = Vec2((float)(i == 1 || i == 2) * 10.f, (float)(i >= 2) * 10.f);
        }
        const u16 idx[6] = {0, 2, 1, 0, 3, 2};
        groundMesh->set_data(v, 4, idx, 6);
        groundMesh->upload();
    }
    Material* groundMat = scene.create_material(0.55f, 0.55f, 0.5f);
    groundMat->diffuse = assets.loadTexture("ground", "assets/textures/rockwall.tga");
    MeshInstance* ground = scene.root().create_child<MeshInstance>("ground");
    ground->set_mesh(groundMesh);
    ground->set_material(groundMat);

    // ── the character ──
    SkinnedMeshInstance* sinbad = scene.root().create_child<SkinnedMeshInstance>("sinbad");
    sinbad->set_mesh(sinbadRes); // materials come from the mesh itself
    sinbad->set_position(0.f, 5.f, 0.f);
    sinbad->animation().layer(0).play("IdleBase");
    sinbad->animation().layer(1).play("IdleTop");

    // ── two swords, sheathed on the back by default (Ogre's own Sinbad
    // sample carries a pair, one per hip) — using Sinbad's own
    // Sheath.L/R and Handle.L/R attachment bones (zero-weight, Ogre-only
    // "helper" bones the original .skeleton carries for exactly this
    // purpose; the exporter now pulls them in even though they skin no
    // vertices). ──
    // materials come from sword.h3d's own MATS chunk via set_mesh() below
    Mesh* swordMesh = assets.loadMesh("sinbad_sword", "assets/models/sinbad/sword.h3d");
    swordMesh->set_material_texture(0, swordTex);

    BoneAttachment* backL = sinbad->create_child<BoneAttachment>("backL");
    backL->attach(sinbad, "Sheath.L");
    BoneAttachment* backR = sinbad->create_child<BoneAttachment>("backR");
    backR->attach(sinbad, "Sheath.R");

    BoneAttachment* handL = sinbad->create_child<BoneAttachment>("handL");
    if (!handL->attach(sinbad, "Handle.L")) handL->attach(sinbad, "Hand.L");
    BoneAttachment* handR = sinbad->create_child<BoneAttachment>("handR");
    if (!handR->attach(sinbad, "Handle.R")) handR->attach(sinbad, "Hand.R");

 

    MeshInstance* swordL = nullptr;
    MeshInstance* swordR = nullptr;
    Node3D* tipL = nullptr; // blade-tip markers for the sword-swipe trail
    Node3D* tipR = nullptr;

    bool swordsDrawn = false;
    if (swordMesh)
    {
        // blade tip in the sword's local space: far end of the mesh's
        // longest bounding-box axis (the hilt sits at/near the origin)
        Vec3 tipOffset(0.f, 0.f, 0.f);
        {
            const BoundingBox& bb = swordMesh->bounds();
            float ex = bb.max.x - bb.min.x;
            float ey = bb.max.y - bb.min.y;
            float ez = bb.max.z - bb.min.z;
            if (ex >= ey && ex >= ez)
                tipOffset.x = fabsf(bb.max.x) >= fabsf(bb.min.x) ? bb.max.x : bb.min.x;
            else if (ey >= ez)
                tipOffset.y = fabsf(bb.max.y) >= fabsf(bb.min.y) ? bb.max.y : bb.min.y;
            else
                tipOffset.z = fabsf(bb.max.z) >= fabsf(bb.min.z) ? bb.max.z : bb.min.z;
        }

        swordL = backL->create_child<MeshInstance>("swordL");
        swordL->set_mesh(swordMesh);
        tipL = swordL->create_child<Node3D>("tipL");
        tipL->set_position(tipOffset);

        swordR = backR->create_child<MeshInstance>("swordR");
        swordR->set_mesh(swordMesh);
        tipR = swordR->create_child<Node3D>("tipR");
        tipR->set_position(tipOffset);
    }

    // ── swoosh: a glowing additive trail per blade that only exists
    // during an actual attack swing — added on click, cleared once the
    // slice animation finishes (not the whole time swords are drawn: a
    // stationary blade held at idle would otherwise keep baking a tiny
    // stub trail from idle-sway jitter and look like a frozen streak). ──
    RibbonTrailNode* beam = scene.root().create_child<RibbonTrailNode>("beam", 2, 60);
    beam->blend = ParticleBlendMode::Additive;
    // depth test stays ON: without it the swoosh shows through the body
    // from any angle, reading as if it cuts inside the character
    beam->setTrailLength(0.02f);
    // gated: only emits during an actual slice — idle bone sway would
    // otherwise keep smearing a stub trail while standing still
    beam->setEmitting(false);

    // sun flare: screen-space, follows -light_dir, occlusion-query faded
    LensFlareNode* flare = scene.root().create_child<LensFlareNode>("sun_flare");

    // time of day: T/G rotate the sun; the procedural sky follows
    // set_light_dir on its own (dawn/day/dusk/night), shadows track too
    float timeOfDay = 0.9f; // radians of sun elevation, ~mid-morning

    Camera3D* camera = scene.root().create_child<Camera3D>("cam");
    camera->set_perspective(50.f, 0.1f, 300.f);
    scene.set_active_camera(camera);
    scene.ready();

    OrbitCam orbit;
    bool running_state = false; // current locomotion state (for edge-triggered crossfades)
    float facingYaw = 0.f;      // character's current facing (radians)

    // sword draw/sheathe: the reparent (back <-> hand) only happens once
    // the DrawSwords clip finishes, not the instant E is pressed — so the
    // blade stays on the back until the animation actually shows it
    // leaving, matching the reference controller's mid-swing bone swap.
    bool transitioning = false;
    bool pendingDrawn = false;
    float transitionTimer = 0.f;

    // swoosh trail: only active for the duration of a slice clip
    const AnimationClip* sliceVClip = sinbadRes->find_clip("SliceVertical");
    const AnimationClip* sliceHClip = sinbadRes->find_clip("SliceHorizontal");
    const float sliceVLen = sliceVClip ? sliceVClip->duration() : 0.4f;
    const float sliceHLen = sliceHClip ? sliceHClip->duration() : 0.4f;
    bool attacking = false;
    float attackTimer = 0.f;
    float attackLen = 0.f;

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
                if (ev.key.keysym.sym == SDLK_e && swordL && !transitioning)
                {
                    pendingDrawn = !swordsDrawn;
                    transitioning = true;
                    transitionTimer = 0.f;
                    sinbad->animation().layer(1).play_one_shot(
                        "DrawSwords", running_state ? "RunTop" : "IdleTop", 0.15f);
                }
                if (ev.key.keysym.sym == SDLK_F9) renderer.set_show_stats(!renderer.show_stats());
                if (ev.key.keysym.sym == SDLK_F10)
                {
                    int gw, gh;
                    app.DrawableSize(&gw, &gh);
                    app.gif.Toggle(gw, gh);
                }
            }
            // attack: only while the swords are actually in hand, not
            // mid-draw/sheathe, and not already mid-swing (no spam-clicking
            // a new slice over the current one) — left click = vertical
            // slice, right = horizontal
            if (ev.type == SDL_MOUSEBUTTONDOWN && swordsDrawn && !transitioning && !attacking)
            {
                const char* returnTo = running_state ? "RunTop" : "IdleTop";
                bool vertical = ev.button.button == SDL_BUTTON_LEFT;
                bool horizontal = ev.button.button == SDL_BUTTON_RIGHT;
                if (vertical) sinbad->animation().layer(1).play_one_shot("SliceVertical", returnTo, 0.1f);
                else if (horizontal)
                    sinbad->animation().layer(1).play_one_shot("SliceHorizontal", returnTo, 0.1f);

                if (vertical || horizontal)
                {
                    attacking = true;
                    attackTimer = 0.f;
                    attackLen = vertical ? sliceVLen : sliceHLen;
                    beam->setEmitting(true); // swoosh only during the swing
                }
            }
            orbit.handle(ev);
        }
        if (!running) break;

        gl::u64 now = SDL_GetPerformanceCounter();
        float dt = (float)((double)(now - lastTicks) / (double)freq);
        lastTicks = now;
        if (dt > 0.1f) dt = 0.1f;

        // ── movement: camera-relative WASD, turn-to-face + run/idle ──
        const Uint8* keys = SDL_GetKeyboardState(nullptr);

        // time of day: sun sweeps a dawn→noon→dusk arc; sky + shadows +
        // flare all derive from the same light dir
        if (keys[SDL_SCANCODE_T]) timeOfDay += dt * 0.4f;
        if (keys[SDL_SCANCODE_G]) timeOfDay -= dt * 0.4f;
        Vec3 sunDir = Vec3(cosf(timeOfDay) * 0.8f, sinf(timeOfDay), 0.35f).normalized();
        renderer.set_light_dir(sunDir * -1.f);
        Vec2 input(0.f, 0.f);
        if (keys[SDL_SCANCODE_W]) input.y -= 1.f;
        if (keys[SDL_SCANCODE_S]) input.y += 1.f;
        if (keys[SDL_SCANCODE_A]) input.x -= 1.f;
        if (keys[SDL_SCANCODE_D]) input.x += 1.f;

        bool moving = (input.x != 0.f || input.y != 0.f);
        if (moving != running_state)
        {
            running_state = moving;
            sinbad->animation().layer(0).cross_fade(moving ? "RunBase" : "IdleBase", 0.25f);
            sinbad->animation().layer(1).cross_fade(moving ? "RunTop" : "IdleTop", 0.25f);
        }

        if (moving)
        {
            float len = sqrtf(input.x * input.x + input.y * input.y);
            input.x /= len;
            input.y /= len;
            Vec3 fwd(-sinf(orbit.yaw), 0.f, -cosf(orbit.yaw));
            Vec3 right(cosf(orbit.yaw), 0.f, -sinf(orbit.yaw));
            Vec3 dir = right * input.x - fwd * input.y;
            dir.y = 0.f;
            if (dir.length_squared() > 1e-8f)
            {
                dir.normalize();
                float goalYaw = atan2f(dir.x, dir.z);
                // shortest angular path
                float delta = goalYaw - facingYaw;
                while (delta > 3.14159265f) delta -= 6.2831853f;
                while (delta < -3.14159265f) delta += 6.2831853f;
                facingYaw += delta * fminf(1.f, kTurnSpeed * dt);
                sinbad->set_rotation(Quaternion::FromAxisAngle(Vec3(0.f, 1.f, 0.f), facingYaw));

                Vec3 p = sinbad->get_position();
                p += dir * (kRunSpeed * dt);
                sinbad->set_position(p);
            }
        }

        // if(attacking)
        //     {
                    
        //             beam->addChain(swordL, Vec4(0.3f, 0.7f, 1.0f, 0.9f),
        //                            Vec4(0.0f, 0.0f, 0.0f, 0.0f), 0.14f, 0.0f, 0.3f);
        //             beam->addChain(swordR, Vec4(0.3f, 0.7f, 1.0f, 0.9f),
        //                            Vec4(0.0f, 0.0f, 0.0f, 0.0f), 0.14f, 0.0f, 0.3f);
        //         }
        //         else
        //         {
        //             beam->clearChains();
        //         }

        if (transitioning)
        {
            transitionTimer += dt;
            if (transitionTimer >= drawSwordsLen)
            {
                transitioning = false;
                swordsDrawn = pendingDrawn;
                // reparent: ownership moves from one attachment to the
                // other, local transform reset so the blade sits naturally
                // at its new anchor
                Node* fromL = swordsDrawn ? backL : handL;
                Node* toL = swordsDrawn ? handL : backL;
                fromL->remove_child(swordL);
                toL->add_child(swordL);
                swordL->set_position(0.f, 0.f, 0.f);
                swordL->set_rotation(Quaternion::Identity());

                Node* fromR = swordsDrawn ? backR : handR;
                Node* toR = swordsDrawn ? handR : backR;
                fromR->remove_child(swordR);
                toR->add_child(swordR);
                swordR->set_position(0.f, 0.f, 0.f);
                swordR->set_rotation(Quaternion::Identity());

 
                if (swordsDrawn)
                {
                    // blade ("swipe") trails: quads span hilt→tip, filling
                    // the whole swept arc. endColor must be zero so the
                    // trail fully fades out under additive blending; the
                    // span shrinks 1→0 back toward the hilt as it fades.
                    beam->addBladeChain(swordL, tipL, Vec4(0.3f, 0.7f, 1.0f, 0.9f),
                                        Vec4(0.0f, 0.0f, 0.0f, 0.0f), 0.3f);
                    beam->addBladeChain(swordR, tipR, Vec4(0.3f, 0.7f, 1.0f, 0.9f),
                                        Vec4(0.0f, 0.0f, 0.0f, 0.0f), 0.3f);
                }
                else
                {
                    beam->clearChains();
                }
            }
        }

        if (attacking)
        {
            attackTimer += dt;
            if (attackTimer >= attackLen)
            {
                attacking = false;
                beam->setEmitting(false); // stop baking; trail fades out
            }
        }

        orbit.apply(camera, sinbad->get_position());

        scene.update(dt);

        int w, h;
        app.DrawableSize(&w, &h);
        renderer.render(scene, w, h);

        app.EndFrame();

        ++frame;
        if (maxFrames > 0 && frame >= maxFrames) running = false;
    }

    if (getenv("COREGL_GIF_AUTOSTART")) app.gif.Toggle(0, 0);
    printf("frames: %d | bones: %d\n", frame, sinbadRes->skeleton().bone_count());
    scene.release_gpu();
    assets.release();
    renderer.release();
    app.Destroy();
    return 0;
}
