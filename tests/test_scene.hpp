#pragma once

// First scene-driven render: a node tree (Camera3D + MeshInstances sharing
// two Mesh resources) collected into flat RenderItems and drawn by a simple
// forward pass. This closes the loop scene -> RenderQueue -> coregl and is
// the seed of the pass architecture: shadow cascades, reflections and water
// are later passes consuming the same collected list.
//
// Controls: WASD move, Q/E down/up, hold left mouse to look, LSHIFT fast,
//           F10 gif, ESC quit. A child node orbits its parent cube to show
//           hierarchy transforms live.

#include "test_common.hpp"
#include <scene/Scene.hpp>
#include <scene/Mesh.hpp>
#include <scene/MeshInstance.hpp>
#include <scene/Camera3D.hpp>

static const char* kSceneFwdVS = R"(#version 430 core
layout(location = 0) in vec3 a_position;
layout(location = 1) in vec3 a_normal;
layout(location = 2) in vec4 a_tangent;
layout(location = 3) in vec2 a_uv;
uniform mat4 u_model;
uniform mat4 u_viewProj;
out vec3 v_normal;
void main()
{
    v_normal = normalize(mat3(u_model) * a_normal);
    gl_Position = u_viewProj * (u_model * vec4(a_position, 1.0));
}
)";

static const char* kSceneFwdFS = R"(#version 430 core
in vec3 v_normal;
out vec4 OutColor;
uniform vec3 u_baseColor;
uniform vec3 u_lightDir;
void main()
{
    float diffuse = max(dot(normalize(v_normal), -u_lightDir), 0.0);
    OutColor = vec4(u_baseColor * (0.30 + 0.70 * diffuse), 1.0);
}
)";

// builds a unit cube Mesh (y in [0,1], 24 verts with face normals)
static void sceneMakeCube(Mesh& mesh)
{
    // clang-format off
    const float P[6][4][3] = {
        {{-.5f,0,-.5f},{-.5f,1,-.5f},{ .5f,1,-.5f},{ .5f,0,-.5f}}, // -Z
        {{-.5f,0, .5f},{ .5f,0, .5f},{ .5f,1, .5f},{-.5f,1, .5f}}, // +Z
        {{-.5f,0,-.5f},{-.5f,0, .5f},{-.5f,1, .5f},{-.5f,1,-.5f}}, // -X
        {{ .5f,0,-.5f},{ .5f,1,-.5f},{ .5f,1, .5f},{ .5f,0, .5f}}, // +X
        {{-.5f,0,-.5f},{ .5f,0,-.5f},{ .5f,0, .5f},{-.5f,0, .5f}}, // -Y
        {{-.5f,1,-.5f},{-.5f,1, .5f},{ .5f,1, .5f},{ .5f,1,-.5f}}, // +Y
    };
    const float N[6][3] = {{0,0,-1},{0,0,1},{-1,0,0},{1,0,0},{0,-1,0},{0,1,0}};
    // clang-format on
    MeshVertex verts[24];
    gl::u16 idx[36];
    for (int f = 0; f < 6; ++f)
    {
        for (int v = 0; v < 4; ++v)
        {
            MeshVertex& mv = verts[f * 4 + v];
            mv.position = Vec3(P[f][v][0], P[f][v][1], P[f][v][2]);
            mv.normal = Vec3(N[f][0], N[f][1], N[f][2]);
            mv.tangent = Vec4(1.f, 0.f, 0.f, 1.f);
            mv.uv = Vec2((float)(v == 1 || v == 2), (float)(v >= 2));
        }
        gl::u16 base = (gl::u16)(f * 4);
        gl::u16* o = &idx[f * 6];
        o[0] = base;
        o[1] = base + 1;
        o[2] = base + 2;
        o[3] = base;
        o[4] = base + 2;
        o[5] = base + 3;
    }
    mesh.set_data(verts, 24, idx, 36);
    mesh.upload();
}

// builds a ground plane Mesh (XZ at y=0, normal +Y)
static void sceneMakePlane(Mesh& mesh, float half)
{
    MeshVertex verts[4];
    const float pos[4][3] = {
        {-half, 0, -half}, {half, 0, -half}, {half, 0, half}, {-half, 0, half}};
    for (int i = 0; i < 4; ++i)
    {
        verts[i].position = Vec3(pos[i][0], pos[i][1], pos[i][2]);
        verts[i].normal = Vec3(0.f, 1.f, 0.f);
        verts[i].tangent = Vec4(1.f, 0.f, 0.f, 1.f);
        verts[i].uv = Vec2((float)(i == 1 || i == 2), (float)(i >= 2));
    }
    const gl::u16 idx[6] = {0, 2, 1, 0, 3, 2}; // CCW seen from above
    mesh.set_data(verts, 4, idx, 6);
    mesh.upload();
}

// spins its parent-relative orbit each frame — proves _update + hierarchy
struct OrbiterNode : Node3D
{
    float angle = 0.f;
    explicit OrbiterNode(const std::string& n) : Node3D(n) {}

protected:
    void _update(float dt) override
    {
        angle += dt * 1.5f;
        set_position(2.5f * cosf(angle), 1.2f, 2.5f * sinf(angle));
    }
};

inline int test_scene(int maxFrames)
{
    TestApp app;
    if (!app.Create("coregl - scene graph render")) return 1;
    printf("controls: WASD move | Q/E down/up | hold left mouse to look | LSHIFT fast\n");

    gl::Shader shader;
    if (!shader.LoadFromString(gl::PipelineStage::VERTEX, kSceneFwdVS) ||
        !shader.LoadFromString(gl::PipelineStage::FRAGMENT, kSceneFwdFS) || !shader.Link())
    {
        fprintf(stderr, "shader error: %s\n", shader.GetLog());
        app.Destroy();
        return 1;
    }
    const gl::i32 modelLoc = shader.GetLocation("u_model");
    const gl::i32 vpLoc = shader.GetLocation("u_viewProj");
    const gl::i32 colorLoc = shader.GetLocation("u_baseColor");
    const gl::i32 lightLoc = shader.GetLocation("u_lightDir");

    // --- resources: TWO meshes shared by many instances ---
    Mesh cubeMesh, planeMesh;
    sceneMakeCube(cubeMesh);
    sceneMakePlane(planeMesh, 40.f);

    // --- the tree ---
    Scene scene;

    MeshInstance* ground = (MeshInstance*)scene.root().add_child(new MeshInstance("ground"));
    ground->set_mesh(&planeMesh);

    // ring of cubes, each with a small orbiting child cube: hierarchy on show
    const int kCubes = 8;
    for (int i = 0; i < kCubes; ++i)
    {
        float a = (float)i / kCubes * 6.28318f;
        MeshInstance* cube = (MeshInstance*)scene.root().add_child(new MeshInstance("cube"));
        cube->set_mesh(&cubeMesh);
        cube->set_position(10.f * cosf(a), 0.f, 10.f * sinf(a));
        cube->set_scale(1.f + (i % 3));

        OrbiterNode* orbit = (OrbiterNode*)cube->add_child(new OrbiterNode("orbit"));
        orbit->angle = a * 2.f;
        MeshInstance* moon = (MeshInstance*)orbit->add_child(new MeshInstance("moon"));
        moon->set_mesh(&cubeMesh);
        moon->set_scale(0.35f);
    }

    Camera3D* camera = (Camera3D*)scene.root().add_child(new Camera3D("camera"));
    camera->set_perspective(55.f, 0.1f, 300.f);
    camera->set_position(0.f, 6.f, 22.f);

    scene.ready();

    const Vec3 lightDir = Vec3(0.5f, -1.0f, 0.3f).normalized();

    // free-fly state drives the camera NODE (not a raw matrix); RADIANS
    float camYaw = 0.f, camPitch = -0.25f;
    bool looking = false;
    gl::u64 lastTicks = SDL_GetPerformanceCounter();
    const gl::u64 freq = SDL_GetPerformanceFrequency();

    std::vector<RenderItem> items;
    items.reserve(64);

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
                    SDL_GL_GetDrawableSize(app.window, &gw, &gh);
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

        // camera node: orientation from yaw/pitch, movement along its axes
        camera->set_euler(Vec3(camPitch, camYaw, 0.f));
        const Uint8* keys = SDL_GetKeyboardState(nullptr);
        float speed = keys[SDL_SCANCODE_LSHIFT] ? 30.f : 10.f;
        if (keys[SDL_SCANCODE_W]) camera->advance(speed * dt);
        if (keys[SDL_SCANCODE_S]) camera->advance(-speed * dt);
        if (keys[SDL_SCANCODE_A]) camera->strafe(-speed * dt);
        if (keys[SDL_SCANCODE_D]) camera->strafe(speed * dt);
        if (keys[SDL_SCANCODE_Q]) camera->move_global(Vec3(0.f, -speed * dt, 0.f));
        if (keys[SDL_SCANCODE_E]) camera->move_global(Vec3(0.f, speed * dt, 0.f));

        scene.update(dt); // orbiters move here

        // --- collect: tree -> flat list, culled by the camera frustum ---
        Frustum frustum;
        frustum.build(camera->get_view_matrix(), camera->get_projection_matrix());
        items.clear();
        scene.collect(items, &frustum);

        // --- forward pass over the collected items ---
        app.BeginFrame();
        int w, h;
        SDL_GL_GetDrawableSize(app.window, &w, &h);
        camera->set_viewport_size(w, h);

        gl::Renderer::SetDepthTest(true);
        gl::Renderer::SetCull(gl::CullMode::BACK);
        gl::Renderer::ClearColor(0.5f, 0.65f, 0.8f, 1.0f);
        gl::Renderer::Clear(true, true);

        shader.Bind();
        Mat4 vp = camera->get_view_projection();
        shader.SetMat4(vpLoc, vp.x);
        shader.SetVec3(lightLoc, lightDir.x, lightDir.y, lightDir.z);

        for (const RenderItem& item : items)
        {
            // no Material yet: color derives from the geometry (plane vs cubes)
            if (item.vao == &planeMesh.vao())
                shader.SetVec3(colorLoc, 0.72f, 0.72f, 0.72f);
            else
                shader.SetVec3(colorLoc, 0.85f, 0.45f, 0.3f);

            shader.SetMat4(modelLoc, item.world.x);
            item.vao->Bind();
            gl::Renderer::DrawIndexed(gl::RenderPrimitive::TRIANGLES, item.index_count,
                                      item.first_index);
        }

        app.EndFrame();
        ++frame;
        if (maxFrames > 0 && frame >= maxFrames) break;
    }

    printf("frames: %d | items/frame: %d\n", frame, (int)items.size());

    shader.Release();
    app.Destroy();
    return 0;
}
