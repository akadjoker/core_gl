// BSP map demo: loads a Quake 3 BSP (IBSP v46) through the AssetManager,
// renders it with CSM shadows, sky, and a first-person camera driven by the
// map's own collision (BspInstance::moveAndSlide — real brush/plane
// collision, see BSPLoader.cpp). Movement math runs in the BSP's own raw
// map-unit space (same space traceBox/moveAndSlide operate in — Quake
// units, ~32/meter), converted to world/render space only for the camera's
// actual position (via mapInst->get_world_matrix(), which folds in the
// 1/32 scale below). Gravity/jump are plain local variables applied by
// hand each frame, same convention as demos/world_test/main.cpp.
//
// Controls: mouse-drag look, WASD walk (ground-relative, not noclip),
//           Space jump, LSHIFT run, F9 stats, P print position,
//           T/G time-of-day, B entity gizmos, F10 gif, ESC quit.

#include "demo_app.hpp"
#include "demo_fly.hpp"
#include <scene/Scene.hpp>
#include <scene/SceneRenderer.hpp>
#include <scene/Material.hpp>
#include <scene/MeshInstance.hpp>
#include <scene/BspInstance.hpp>
#include <scene/LightNode.hpp>
#include <scene/Mesh.hpp>
#include <scene/AssetManager.hpp>
#include <scene/Filesystem.hpp>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <vector>

int main(int argc, char** argv)
{
    const int maxFrames = (argc > 1) ? atoi(argv[1]) : 0;

    DemoApp app;
    if (!app.Create("coregl - bsp")) return 1;
    printf("controls: mouse-drag look | WASD walk | Space jump | LSHIFT run | F9 stats | P print pos | T/G time-of-day | B entity gizmos | V crosshair pick | F10 gif\n");

    SceneRenderer renderer;
    if (!renderer.init())
    {
        fprintf(stderr, "renderer init failed\n");
        app.Destroy();
        return 1;
    }
    renderer.set_light_dir(Vec3(0.4f, -0.8f, 0.3f));
    renderer.set_sky_enabled(true);
    renderer.enable_shadows(2, 1024, 200.f);
    if (getenv("COREGL_STATS")) renderer.set_show_stats(true);
    if (getenv("COREGL_CASCADES")) renderer.set_show_cascades(true);

    Scene scene;
    assets::AssetManager& assets = assets::AssetManager::instance();

    // ---- filesystem: register asset paths so textures resolve automatically ---
    fs::getFilesystem().addFolder("assets/bsp/oa_rpg3dm2");
    fs::getFilesystem().addFolder("assets/bsp/map-20kdm2");
    fs::getFilesystem().addFolder("/media/projectos/assets/terror");
    

    // ---- scene graph node (created first so its collision data can be
    // filled in the SAME load_bsp_mesh call below — one file read total,
    // not one for the mesh and a second for collision) ------------------------
    BspInstance* mapInst = scene.root().create_child<BspInstance>("bsp_map");

    // ---- load BSP map -------------------------------------------------------
    auto t0 = std::chrono::steady_clock::now();
    std::vector<Material*> materials;
    // textureDir is the map root — BSP texture names like
    // "textures/base_floor/clang" are cleaned and joined: root + "textures/..." + ext
    
    // Mesh* bspMesh = assets.load_bsp_mesh("bsp_map",
    //                                       "assets/bsp/oa_rpg3dm2/oa_rpg3dm2.bsp",
    //                                       materials,
    //                                       "assets/bsp/oa_rpg3dm2/", mapInst);
//  Mesh* bspMesh = assets.load_bsp_mesh("bsp_map",
//                                           "assets/bsp/map-20kdm2/20kdm2.bsp",
//                                           materials,
//                                           "assets/bsp/map-20kdm2/",
//                                           mapInst);

    // textureDir empty: BSP texture names already come as "textures/..."
    // relative to the mod root, and that root is registered via addFolder
    // above — fs::getFilesystem() searches it automatically, no need to
    // hardcode an absolute, machine-specific path here.
    Mesh* bspMesh = assets.load_bsp_mesh("bsp_map",
                                          "maps/ut4_inferno_b3.bsp",
                                          materials,
                                          "",
                                          mapInst);


    
                                         auto t1 = std::chrono::steady_clock::now();

    if (!bspMesh)
    {
        fprintf(stderr, "BSP load failed (run from repo root)\n");
        app.Destroy();
        return 1;
    }

    const double loadMs = std::chrono::duration<double, std::milli>(t1 - t0).count();
    printf("bsp: %u surfaces, %u verts, %u indices, %u materials, %u entities | load %.1fms\n",
           (unsigned)bspMesh->surfaces().size(), bspMesh->vertex_count(),
           bspMesh->index_count(), (unsigned)materials.size(),
           (unsigned)mapInst->entities().size(), loadMs);

 

    mapInst->set_mesh(bspMesh);
    mapInst->set_materials(materials);
    // Q3 maps are in game units (~32 units = 1m); scale down for a
    // reasonable camera speed. Adjust this to taste for each map.
    mapInst->set_scale(0.03125f); // 1/32
    mapInst->remove_entities("target_speaker");
    mapInst->remove_entities("info_ut_spawn");
    mapInst->remove_entities("info_null");

    // headlamp: point light following the camera for dark corners
    PointLight* headlamp = scene.root().create_child<PointLight>("headlamp");
    headlamp->color = Vec3(1.f, 1.f, 0.95f);
    headlamp->intensity = 200.0f;
    headlamp->range = 5000.f;
    headlamp->cast_shadows = false;

    Camera3D* camera = scene.root().create_child<Camera3D>("fly");
    camera->set_perspective(60.f, 0.1f, 500.f);
    camera->set_position(0.f, 3.f, 0.f);

    scene.set_active_camera(camera);
    scene.ready();

    // ---- player: a box swept through the map's own BSP tree (real brush
    // collision, not noclip) — moveAndSlide runs entirely in raw map-unit
    // space; only the camera's final position gets converted to world
    // space via mapInst's transform (see mapXform below). Half-extent is
    // Quake 3's own standing player bbox (mins/maxs (-16,-16,-24)/(16,16,32)
    // ~= 56 units tall), used symmetric around playerPos here for
    // simplicity, so playerPos tracks the box CENTER, not the feet. ------
    const Vec3 playerHalfExtent(16.f, 28.f, 16.f);
    const float playerGravity = 800.f;   // Quake units/s^2
    const float playerJumpSpeed = 270.f; // Quake units/s
    const float playerWalkSpeed = 320.f; // Quake units/s (default sv_speed)
    Vec3 spawnPos;
    if (!mapInst->find_spawn_point(spawnPos)) spawnPos = Vec3(0.f, 64.f, 0.f);
    // Quake spawn "origin" is at the player's FEET; playerPos tracks the box
    // CENTER, so lift it by exactly one half-extent — anything more pushes
    // the box into the ceiling headroom was sized for (a real bug this demo
    // hit: a "+8 extra buffer" here left the player permanently stuck in
    // solid geometry from frame 0, moveAndSlide's fraction=0 every frame).
    Vec3 playerPos = spawnPos + Vec3(0.f, playerHalfExtent.y, 0.f);
    float playerVelY = 0.f;
    bool onGround = false;
    // tryStepUp snaps playerPos.y instantly when it climbs a stair riser —
    // fine for collision, but a camera that jumps by a whole step height in
    // one frame reads as jitter/shake. Track a SEPARATE smoothed eye height
    // that chases playerPos.y at a bounded rate instead of copying it
    // directly, same idea as Source's view-punch/step smoothing — only Y is
    // smoothed, X/Z stay direct so turning/strafing never feels laggy.
    float smoothEyeY = playerPos.y;
    camera->set_position(mapInst->get_world_matrix() * playerPos);

    FlyCam fly; // mouse-look only here — WASD below drives playerPos through moveAndSlide instead of fly.apply()'s noclip
    gl::u64 lastTicks = SDL_GetPerformanceCounter();
    const gl::u64 freq = SDL_GetPerformanceFrequency();
    float timeOfDay = getenv("COREGL_TOD") ? (float)atof(getenv("COREGL_TOD")) : 0.9f;
    bool showEntityGizmos = false; // B toggles — see BspInstance::entities()
    bool showPick = true; // V toggles — see BspInstance::raycast()

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
                if (ev.key.keysym.sym == SDLK_F9)
                    renderer.set_show_stats(!renderer.show_stats());
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
                if (ev.key.keysym.sym == SDLK_p)
                {
                    Vec3 p = camera->get_position();
                    printf("camera pos (%.2f, %.2f, %.2f) yaw=%.3f pitch=%.3f\n",
                           p.x, p.y, p.z, fly.yaw, fly.pitch);
                }
                if (ev.key.keysym.sym == SDLK_F10)
                {
                    int gw, gh;
                    app.DrawableSize(&gw, &gh);
                    app.gif.Toggle(gw, gh);
                }
                if (ev.key.keysym.sym == SDLK_b)
                {
                    showEntityGizmos = !showEntityGizmos;
                    printf("entity gizmos: %s\n", showEntityGizmos ? "ON" : "off");
                }
                if (ev.key.keysym.sym == SDLK_v)
                {
                    showPick = !showPick;
                    printf("crosshair pick: %s\n", showPick ? "ON" : "off");
                }
                if (ev.key.keysym.sym == SDLK_SPACE && onGround)
                {
                    playerVelY = playerJumpSpeed;
                    onGround = false;
                }
            }
            fly.handle(ev);
        }
        if (!running) break;

        gl::u64 now = SDL_GetPerformanceCounter();
        float dt = (float)((double)(now - lastTicks) / (double)freq);
        lastTicks = now;
        if (dt > 0.1f) dt = 0.1f;

        // time-of-day: T/G move the sun
        const Uint8* keys = SDL_GetKeyboardState(nullptr);
        if (keys[SDL_SCANCODE_T]) timeOfDay += dt * 0.4f;
        if (keys[SDL_SCANCODE_G]) timeOfDay -= dt * 0.4f;
        Vec3 sunDir = Vec3(cosf(timeOfDay) * 0.8f, sinf(timeOfDay), 0.35f).normalized();
        renderer.set_light_dir(sunDir * -1.f);

        // look only (fly.apply() would also noclip-move the camera —
        // WASD below drives playerPos through moveAndSlide instead)
        camera->set_euler(Vec3(fly.pitch, fly.yaw, 0.f));

        // ---- ground-relative WASD (camera forward/right flattened to the
        // XZ plane, like any FPS — straight camera->advance() would let
        // looking down make you walk into the floor) ------------------------
        Vec3 fwd = camera->forward(); fwd.y = 0.f;
        if (fwd.length_squared() > 1e-8f) fwd = fwd.normalized();
        Vec3 rgt = camera->right(); rgt.y = 0.f;
        if (rgt.length_squared() > 1e-8f) rgt = rgt.normalized();

        Vec3 wishDir(0.f, 0.f, 0.f);
        if (keys[SDL_SCANCODE_W]) wishDir += fwd;
        if (keys[SDL_SCANCODE_S]) wishDir -= fwd;
        if (keys[SDL_SCANCODE_D]) wishDir += rgt;
        if (keys[SDL_SCANCODE_A]) wishDir -= rgt;
        if (wishDir.length_squared() > 1e-8f) wishDir = wishDir.normalized();

        float speed = keys[SDL_SCANCODE_LSHIFT] ? playerWalkSpeed * 1.8f : playerWalkSpeed;

        // gravity: plain position integration, game-side — moveAndSlide
        // never sees playerVelY itself, only the resulting target position
        // (same convention as demos/world_test/main.cpp)
        playerVelY -= playerGravity * dt;
        if (playerVelY < -800.f) playerVelY = -800.f; // terminal velocity

        Vec3 target = playerPos + wishDir * (speed * dt) + Vec3(0.f, playerVelY * dt, 0.f);
        playerPos = mapInst->moveAndSlide(playerPos, target, playerHalfExtent,
                                          BspCollisionResponse::SlideXZ, &onGround);
        if (onGround && playerVelY < 0.f) playerVelY = 0.f; // landed — stop accumulating fall speed

        // Smooth the eye height toward playerPos.y instead of copying it
        // directly: a stair-step snap (tryStepUp, up to 24 units in one
        // frame) eases in over a few frames instead of jump-cutting the
        // view. A real fall/landing (much bigger than any single riser) is
        // NOT smoothed — snapping straight to it keeps the ground/gravity
        // feel responsive instead of floaty.
        {
            const float dyStep = playerPos.y - smoothEyeY;
            const float snapThreshold = 40.f; // bigger than any single stair riser (kStepHeight=24)
            if (std::fabs(dyStep) > snapThreshold)
                smoothEyeY = playerPos.y;
            else
                smoothEyeY += dyStep * std::min(1.f, 15.f * dt);
        }
        Vec3 eyePos(playerPos.x, smoothEyeY + 22.f, playerPos.z);

        camera->set_position(mapInst->get_world_matrix() * eyePos);
        headlamp->set_position(camera->get_position());
        headlamp->set_position(camera->get_position());

        scene.update(dt);

        int w, h;
        app.DrawableSize(&w, &h);
        renderer.render(scene, w, h);

        // ---- entity gizmos: no render mesh of their own in the BSP, so
        // draw a placeholder per parsed entity (B toggles), colored by
        // rough category so it's actually readable at a glance (matches
        // the classname breakdown printed at load time):
        //   orange box  = brush entity (func_door/func_water/trigger_push/...)
        //   yellow sphere = light
        //   green sphere  = player spawn (info_player_*)
        //   magenta box   = pickup (item_*/weapon_*/ammo_*)
        //   red box       = logic-only target/waypoint (target_*)
        //   blue box      = anything else
        if (showEntityGizmos)
        {
            const Mat4& mapXform = mapInst->get_world_matrix();
            const float mapScale = mapInst->get_scale().x;
            auto startsWith = [](const std::string& s, const char* prefix) {
                return s.compare(0, strlen(prefix), prefix) == 0;
            };
            for (const BspInstance::Entity& e : mapInst->entities())
            {
                if (e.classname.empty() || e.classname == "worldspawn") continue;

                Vec3 bmin, bmax;
                if (mapInst->entity_model_bounds(e, bmin, bmax))
                {
                    Vec3 center = mapXform * ((bmin + bmax) * 0.5f);
                    Vec3 halfExtent = (bmax - bmin) * (0.5f * mapScale);
                    renderer.draw_wire_box(*camera, center, halfExtent, 255, 140, 60, 220);
                    renderer.draw_world_text(*camera, center + Vec3(0.f, halfExtent.y + 0.3f, 0.f),
                                            e.classname.c_str(), w, h, 13.f, 255, 190, 120, 255);
                    continue;
                }

                Vec3 origin;
                if (!BspInstance::entity_origin(e, origin)) continue;
                Vec3 pos = mapXform * origin;

                gl::u8 tr = 220, tg = 220, tb = 220;
                if (e.classname == "light")
                {
                    renderer.draw_wire_sphere(*camera, pos, 0.3f, 255, 220, 60, 255);
                    tr = 255; tg = 220; tb = 60;
                }
                else if (startsWith(e.classname, "info_player"))
                {
                    renderer.draw_wire_sphere(*camera, pos, 0.25f, 60, 255, 100, 255);
                    tr = 60; tg = 255; tb = 100;
                }
                else if (startsWith(e.classname, "item_") || startsWith(e.classname, "weapon_") ||
                        startsWith(e.classname, "ammo_"))
                {
                    renderer.draw_wire_box(*camera, pos, Vec3(0.2f), 255, 60, 220, 255);
                    tr = 255; tg = 60; tb = 220;
                }
                else if (startsWith(e.classname, "target_"))
                {
                    renderer.draw_wire_box(*camera, pos, Vec3(0.1f), 255, 60, 60, 180);
                    tr = 255; tg = 100; tb = 100;
                }
                else
                {
                    renderer.draw_wire_box(*camera, pos, Vec3(0.15f), 60, 220, 255, 255);
                    tr = 60; tg = 220; tb = 255;
                }
                renderer.draw_world_text(*camera, pos + Vec3(0.f, 0.4f, 0.f), e.classname.c_str(), w, h,
                                        13.f, tr, tg, tb, 255);
            }
        }

        // ---- crosshair pick: raycast straight down the camera's view
        // (BspInstance::raycast — a zero-size traceBox, see BSPLoader.cpp)
        // and mark the hit point + distance, same idea as a level editor's
        // "what am I looking at" readout. Toggle with V.
        if (showPick)
        {
            BspRayHit pick = mapInst->raycast(eyePos, camera->forward(), 8192.f);
            if (pick.hit)
            {
                Vec3 mapXform_pos = mapInst->get_world_matrix() * pick.point;
                renderer.draw_wire_sphere(*camera, mapXform_pos, 0.12f, 255, 255, 255, 255);
                char buf[64];
                snprintf(buf, sizeof(buf), "%.0fu", pick.distance);
                renderer.draw_world_text(*camera, mapXform_pos + Vec3(0.f, 0.25f, 0.f), buf, w, h, 13.f,
                                        255, 255, 255, 255);
            }
        }

        app.EndFrame();

        ++frame;
        if (maxFrames > 0 && frame >= maxFrames) running = false;
    }

    printf("frames: %d\n", frame);

    // materials are owned by the Mesh (load_bsp_mesh calls
    // set_owned_materials) — freed by assets.release() below, `materials`
    // is only a view here; deleting them ourselves double-frees.

    scene.release_gpu();
    assets.release(); // frees the BSP mesh + textures
    renderer.release();
    app.Destroy();
    return 0;
}
