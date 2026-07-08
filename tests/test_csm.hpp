#pragma once

// Cascaded Shadow Mapping (CSM) with a free camera: a large ground plane and
// dozens of cubes scattered across the world, one fixed directional light.
// CSM splits the camera's view frustum into slices and fits one shadow map
// (one layer of a depth texture array) tightly around each slice — high
// shadow resolution near the camera, lower far away.
//
// Controls:
//   WASD        move           Q/E          down/up
//   left mouse  hold + drag to look around
//   LSHIFT      move faster
//   C           toggle cascade visualization (tints each cascade)
//   F10         record GIF     ESC          quit
//
// Cascade matrix (per frame, per cascade — from tmp/calulatecs.txt):
// 1. take this cascade's near/far slice of the camera frustum
// 2. unproject the 8 clip-space corners to world space via inverse(proj*view)
// 3. center = average of corners; light view looks at it along the sun dir
// 4. tight AABB of the corners in light space, Z range extended so casters
//    outside the slice (e.g. a tall cube behind you, toward the sun) still
//    land in the map
// 5. snap the AABB to shadow-texel-sized steps so the shadow edge doesn't
//    shimmer as the camera moves ("texel snapping")
// 6. cascadeMatrix = ortho(AABB) * lightView

#include "test_common.hpp"
#include "math/Math.hpp"

#define CSM_CASCADES 4

static const char* kCsmDepthVS = R"(#version 430 core
layout(location = 0) in vec3 a_position;
uniform mat4 u_lightMVP;
void main()
{
    gl_Position = u_lightMVP * vec4(a_position, 1.0);
}
)";

static const char* kCsmDepthFS = R"(#version 430 core
void main()
{
}
)";

static const char* kCsmSceneVS = R"(#version 430 core
layout(location = 0) in vec3 a_position;
layout(location = 1) in vec3 a_normal;
uniform mat4 u_model;
uniform mat4 u_viewProj;
uniform mat4 u_view;
out vec3 v_worldPos;
out vec3 v_normal;
out float v_viewDepth;
void main()
{
    vec4 worldPos = u_model * vec4(a_position, 1.0);
    v_worldPos = worldPos.xyz;
    // axis-aligned normals + axis-aligned scaling: mat3(model)+normalize is exact
    v_normal = normalize(mat3(u_model) * a_normal);
    v_viewDepth = -(u_view * worldPos).z; // positive distance in front of the camera
    gl_Position = u_viewProj * worldPos;
}
)";

// Per-fragment cascade selection: pick the first cascade whose far edge is
// beyond this fragment's view depth, then do the usual shadow-map compare
// against that cascade's layer of the depth array.
static const char* kCsmSceneFS = R"(#version 430 core
in vec3 v_worldPos;
in vec3 v_normal;
in float v_viewDepth;
out vec4 OutColor;
uniform sampler2DArray u_shadowMap;
uniform mat4 u_lightViewProj[4];
uniform float u_splits[4]; // far edge of each cascade, in view depth
uniform vec3 u_baseColor;
uniform vec3 u_lightDir; // direction the light travels (sun -> ground)
uniform int u_showCascades;

const vec3 kCascadeTint[4] = vec3[4](
    vec3(1.0, 0.6, 0.6), vec3(0.6, 1.0, 0.6), vec3(0.6, 0.6, 1.0), vec3(1.0, 1.0, 0.6));

void main()
{
    int layer = 3;
    for (int i = 0; i < 4; ++i)
    {
        if (v_viewDepth < u_splits[i])
        {
            layer = i;
            break;
        }
    }

    vec4 lightPos = u_lightViewProj[layer] * vec4(v_worldPos, 1.0);
    vec3 proj = lightPos.xyz / lightPos.w;
    proj = proj * 0.5 + 0.5;

    float shadow = 1.0;
    if (proj.x >= 0.0 && proj.x <= 1.0 && proj.y >= 0.0 && proj.y <= 1.0 && proj.z <= 1.0)
    {
        float shadowMapDepth = texture(u_shadowMap, vec3(proj.xy, float(layer))).r;
        float bias = 0.002;
        if (proj.z - bias > shadowMapDepth) shadow = 0.0;
    }

    // lambert diffuse gated by the shadow term, plus a small ambient floor so
    // shadowed/unlit sides read as dark gray instead of pure black
    float diffuse = max(dot(normalize(v_normal), -u_lightDir), 0.0);
    vec3 color = u_baseColor * (0.30 + 0.70 * diffuse * shadow);
    if (u_showCascades != 0) color *= kCascadeTint[layer];
    OutColor = vec4(color, 1.0);
}
)";

static const float* csmMat4ptr(const Mat4& m)
{
    return m.x;
}

// ---------------------------------------------------------------------------
// Cascade split distances: log/uniform blend (finer slices near the camera)
// ---------------------------------------------------------------------------
static void csmComputeSplits(float nearClip, float farClip, float* splits, int numCascades)
{
    float logBase = logf(farClip / nearClip) / (float)numCascades;
    float uniformFactor = 1.0f / (float)numCascades;

    splits[0] = nearClip;
    for (int i = 1; i <= numCascades; ++i)
    {
        float logSplit = nearClip * expf(logBase * (float)i);
        float unifSplit = nearClip + uniformFactor * (float)i * (farClip - nearClip);
        float blend = (float)i / (float)numCascades;
        splits[i] = logSplit + (unifSplit - logSplit) * (blend * 0.5f);
    }
}

// ---------------------------------------------------------------------------
// One cascade's lightProjection * lightView (algorithm from tmp/calulatecs.txt)
// ---------------------------------------------------------------------------
static Mat4 csmComputeCascadeMatrix(int cascadeIndex, float aspectRatio, float fovDeg,
                                    const Mat4& viewMatrix, const float* splits,
                                    const Vec3& lightDir, float shadowMapSize)
{
    float nearPlane = splits[cascadeIndex];
    float farPlane = splits[cascadeIndex + 1];

    Mat4 projection =
        Mat4::Perspective((double)fovDeg, (double)aspectRatio, (double)nearPlane, (double)farPlane);
    Mat4 invViewProj = Mat4::Inverse(projection * viewMatrix);

    Vec4 corners[8] = {
        Vec4(-1.f, -1.f, -1.f, 1.f), Vec4(-1.f, -1.f, 1.f, 1.f), Vec4(-1.f, 1.f, -1.f, 1.f),
        Vec4(-1.f, 1.f, 1.f, 1.f),   Vec4(1.f, -1.f, -1.f, 1.f), Vec4(1.f, -1.f, 1.f, 1.f),
        Vec4(1.f, 1.f, -1.f, 1.f),   Vec4(1.f, 1.f, 1.f, 1.f),
    };
    for (int i = 0; i < 8; ++i)
    {
        Vec4 world = invViewProj * corners[i];
        if (fabsf(world.w) > 1e-6f) corners[i] = world / world.w;
    }

    Vec3 center(0.f, 0.f, 0.f);
    for (int i = 0; i < 8; ++i)
        center += Vec3(corners[i].x, corners[i].y, corners[i].z);
    center *= (1.0f / 8.0f);

    // eye placed toward the sun (against the light's travel direction),
    // looking at the slice center; tilted up vector avoids a singular basis
    // when the light is nearly vertical
    Vec3 eye = center - lightDir;
    Mat4 lightView = Mat4::LookAt(eye, center, Vec3(0.001f, 1.f, 0.001f));

    Vec3 minV(1e30f, 1e30f, 1e30f);
    Vec3 maxV(-1e30f, -1e30f, -1e30f);
    for (int i = 0; i < 8; ++i)
    {
        Vec3 t = lightView * Vec3(corners[i].x, corners[i].y, corners[i].z);
        minV = minV.Min(t);
        maxV = maxV.Max(t);
    }

    // extend the Z range so shadow casters OUTSIDE this slice (between it
    // and the sun, or just behind it) still render into the map — without
    // this, a tall cube behind the camera casts no shadow into the slice
    const float depthScale = 5.0f;
    minV.z *= (minV.z < 0.f) ? depthScale : (1.0f / depthScale);
    maxV.z *= (maxV.z > 0.f) ? depthScale : (1.0f / depthScale);

    // texel snapping: quantize the AABB to shadow-texel steps so the ortho
    // window moves in whole texels as the camera moves (stable shadow edges)
    float texelX = (maxV.x - minV.x) / shadowMapSize;
    float texelY = (maxV.y - minV.y) / shadowMapSize;
    if (texelX < 1e-6f) texelX = 1e-6f;
    if (texelY < 1e-6f) texelY = 1e-6f;
    minV.x = floorf(minV.x / texelX) * texelX;
    minV.y = floorf(minV.y / texelY) * texelY;
    maxV.x = floorf(maxV.x / texelX) * texelX;
    maxV.y = floorf(maxV.y / texelY) * texelY;

    Mat4 lightProj = Mat4::Ortho(minV.x, maxV.x, minV.y, maxV.y, -maxV.z, -minV.z);
    return lightProj * lightView;
}

inline int test_csm(int maxFrames)
{
    TestApp app;
    if (!app.Create("coregl - cascaded shadow maps")) return 1;
    printf("controls: WASD move | Q/E down/up | hold left mouse to look | LSHIFT fast | C "
           "cascade tint | F10 gif | ESC quit\n");

    gl::Shader depthShader, sceneShader;
    if (!depthShader.LoadFromString(gl::PipelineStage::VERTEX, kCsmDepthVS) ||
        !depthShader.LoadFromString(gl::PipelineStage::FRAGMENT, kCsmDepthFS) ||
        !depthShader.Link() ||
        !sceneShader.LoadFromString(gl::PipelineStage::VERTEX, kCsmSceneVS) ||
        !sceneShader.LoadFromString(gl::PipelineStage::FRAGMENT, kCsmSceneFS) ||
        !sceneShader.Link())
    {
        fprintf(stderr, "shader error: %s%s\n", depthShader.GetLog(), sceneShader.GetLog());
        app.Destroy();
        return 1;
    }

    // --- ground plane: 200x200 units so the far cascades have real work ---
    const float planeHalf = 100.0f;
    const float planeVerts[] = {
        // x, y, z, nx, ny, nz
        -planeHalf, 0.f, -planeHalf, 0.f, 1.f, 0.f, planeHalf,  0.f, -planeHalf, 0.f, 1.f, 0.f,
        planeHalf,  0.f, planeHalf,  0.f, 1.f, 0.f, -planeHalf, 0.f, planeHalf,  0.f, 1.f, 0.f,
    };
    const gl::u16 planeIndices[] = {0, 2, 1, 0, 3, 2}; // same winding as test_shadowmap.hpp
    gl::Buffer planeVbo, planeIbo;
    planeVbo.Allocate(gl::BufferType::ARRAY, planeVerts, sizeof(planeVerts),
                      gl::UsageType::STATIC_DRAW);
    planeIbo.Allocate(gl::BufferType::ELEMENT_ARRAY, planeIndices, sizeof(planeIndices),
                      gl::UsageType::STATIC_DRAW);
    const gl::VertexAttrib posLayout[] = {
        {gl::VertexAttribType::FLOAT, 3, 0, false}, // position
        {gl::VertexAttribType::FLOAT, 3, 0, false}, // normal
    };
    gl::VertexArray planeVao;
    planeVao.AddVertexBuffer(planeVbo, posLayout, 2, 6 * sizeof(float));
    planeVao.SetIndexBuffer(planeIbo, gl::VertexAttribType::USHORT);

    // --- unit cube (y in [0,1]), reused across the world via model matrices ---
    // clang-format off
    const float cubeVerts[] = {
        // x, y, z, nx, ny, nz — 4 verts per face, face normal on each
        -0.5f,0.f,-0.5f, 0,0,-1,  -0.5f,1.f,-0.5f, 0,0,-1,   0.5f,1.f,-0.5f, 0,0,-1,   0.5f,0.f,-0.5f, 0,0,-1, // -Z
        -0.5f,0.f, 0.5f, 0,0, 1,   0.5f,0.f, 0.5f, 0,0, 1,   0.5f,1.f, 0.5f, 0,0, 1,  -0.5f,1.f, 0.5f, 0,0, 1, // +Z
        -0.5f,0.f,-0.5f,-1,0, 0,  -0.5f,0.f, 0.5f,-1,0, 0,  -0.5f,1.f, 0.5f,-1,0, 0,  -0.5f,1.f,-0.5f,-1,0, 0, // -X
         0.5f,0.f,-0.5f, 1,0, 0,   0.5f,1.f,-0.5f, 1,0, 0,   0.5f,1.f, 0.5f, 1,0, 0,   0.5f,0.f, 0.5f, 1,0, 0, // +X
        -0.5f,0.f,-0.5f, 0,-1,0,   0.5f,0.f,-0.5f, 0,-1,0,   0.5f,0.f, 0.5f, 0,-1,0,  -0.5f,0.f, 0.5f, 0,-1,0, // -Y
        -0.5f,1.f,-0.5f, 0, 1,0,  -0.5f,1.f, 0.5f, 0, 1,0,   0.5f,1.f, 0.5f, 0, 1,0,   0.5f,1.f,-0.5f, 0, 1,0, // +Y
    };
    // clang-format on
    gl::u16 cubeIndices[36];
    for (int face = 0; face < 6; ++face)
    {
        gl::u16 base = (gl::u16)(face * 4);
        gl::u16* idx = &cubeIndices[face * 6];
        idx[0] = base;
        idx[1] = base + 1;
        idx[2] = base + 2;
        idx[3] = base;
        idx[4] = base + 2;
        idx[5] = base + 3;
    }
    gl::Buffer cubeVbo, cubeIbo;
    cubeVbo.Allocate(gl::BufferType::ARRAY, cubeVerts, sizeof(cubeVerts),
                     gl::UsageType::STATIC_DRAW);
    cubeIbo.Allocate(gl::BufferType::ELEMENT_ARRAY, cubeIndices, sizeof(cubeIndices),
                     gl::UsageType::STATIC_DRAW);
    gl::VertexArray cubeVao;
    cubeVao.AddVertexBuffer(cubeVbo, posLayout, 2, 6 * sizeof(float));
    cubeVao.SetIndexBuffer(cubeIbo, gl::VertexAttribType::USHORT);

    // scatter cubes across the plane with a deterministic LCG
    const int kNumCubes = 48;
    Mat4 cubeModels[kNumCubes];
    Vec3 cubeColors[kNumCubes];
    gl::u32 rng = 1234;
    for (int i = 0; i < kNumCubes; ++i)
    {
        rng = rng * 1664525u + 1013904223u;
        float x = ((rng >> 8 & 1023) / 1023.f) * 160.f - 80.f;
        rng = rng * 1664525u + 1013904223u;
        float z = ((rng >> 8 & 1023) / 1023.f) * 160.f - 80.f;
        rng = rng * 1664525u + 1013904223u;
        float sxz = 0.8f + ((rng >> 8 & 255) / 255.f) * 2.2f; // footprint 0.8..3
        rng = rng * 1664525u + 1013904223u;
        float sy = 1.0f + ((rng >> 8 & 255) / 255.f) * 5.0f; // height 1..6

        cubeModels[i] = Mat4::Translate(x, 0.f, z) * Mat4::Scale(sxz, sy, sxz);
        rng = rng * 1664525u + 1013904223u;
        cubeColors[i] = Vec3(0.5f + (rng >> 8 & 127) / 255.f, 0.35f + (rng >> 16 & 63) / 255.f,
                             0.3f + (rng >> 22 & 63) / 255.f);
    }

    // --- shadow maps: one depth array layer per cascade ---
    const int kShadowSize = 2048;
    gl::Texture shadowDepth;
    shadowDepth.LoadDepthArray(kShadowSize, kShadowSize, CSM_CASCADES);
    shadowDepth.SetWrap(gl::TextureWrap::CLAMP_TO_EDGE, gl::TextureWrap::CLAMP_TO_EDGE);
    gl::FrameBuffer shadowFbo;
    shadowFbo.AttachTextureLayer(shadowDepth, gl::Attachment::DEPTH, 0);
    shadowFbo.SetDrawBuffers();
    if (!shadowFbo.IsComplete())
    {
        fprintf(stderr, "shadow framebuffer incomplete\n");
        app.Destroy();
        return 1;
    }

    const gl::i32 depthMvpLoc = depthShader.GetLocation("u_lightMVP");
    const gl::i32 sceneModelLoc = sceneShader.GetLocation("u_model");
    const gl::i32 sceneVpLoc = sceneShader.GetLocation("u_viewProj");
    const gl::i32 sceneViewLoc = sceneShader.GetLocation("u_view");
    const gl::i32 sceneColorLoc = sceneShader.GetLocation("u_baseColor");
    const gl::i32 sceneShowLoc = sceneShader.GetLocation("u_showCascades");
    const gl::i32 sceneLightDirLoc = sceneShader.GetLocation("u_lightDir");
    // introspection reports arrays as "name[0]"; consecutive elements occupy
    // consecutive locations on desktop drivers
    const gl::i32 sceneCascadeMatLoc = sceneShader.GetLocation("u_lightViewProj[0]");
    const gl::i32 sceneSplitLoc = sceneShader.GetLocation("u_splits[0]");
    sceneShader.SetInt("u_shadowMap", 0);

    Mat4 planeModel; // identity

    const float kNear = 0.1f, kFar = 200.0f;
    float splits[CSM_CASCADES + 1];
    csmComputeSplits(kNear, kFar, splits, CSM_CASCADES);
    printf("cascade splits: %.2f | %.2f | %.2f | %.2f | %.2f\n", splits[0], splits[1], splits[2],
           splits[3], splits[4]);

    const Vec3 lightDir = Vec3(0.5f, -1.0f, 0.3f).normalized(); // fixed sun

    // --- free camera state ---
    Vec3 camPos(0.f, 5.f, 20.f);
    float camYaw = 0.f; // degrees, 0 = looking down -Z
    float camPitch = -10.f;
    bool showCascades = false;
    bool looking = false;

    gl::u64 lastTicks = SDL_GetPerformanceCounter();
    const gl::u64 freq = SDL_GetPerformanceFrequency();

    Mat4 cascadeMatrices[CSM_CASCADES];

    int frame = 0;
    bool running = true;
    while (running)
    {
        // --- input (own loop: TestApp::PollEvents doesn't expose the mouse) ---
        SDL_Event ev;
        while (SDL_PollEvent(&ev))
        {
            if (ev.type == SDL_QUIT) running = false;
            if (ev.type == SDL_KEYDOWN)
            {
                if (ev.key.keysym.sym == SDLK_ESCAPE) running = false;
                if (ev.key.keysym.sym == SDLK_c) showCascades = !showCascades;
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
                camYaw -= (float)ev.motion.xrel * 0.25f;
                camPitch -= (float)ev.motion.yrel * 0.25f;
                if (camPitch > 89.f) camPitch = 89.f;
                if (camPitch < -89.f) camPitch = -89.f;
            }
        }
        if (!running) break;

        gl::u64 now = SDL_GetPerformanceCounter();
        float dt = (float)((double)(now - lastTicks) / (double)freq);
        lastTicks = now;
        if (dt > 0.1f) dt = 0.1f;

        const float yawRad = camYaw * 0.01745329252f;
        const float pitchRad = camPitch * 0.01745329252f;
        Vec3 forward(-sinf(yawRad) * cosf(pitchRad), sinf(pitchRad),
                     -cosf(yawRad) * cosf(pitchRad));
        forward = forward.normalized();
        Vec3 right = Vec3::Cross(forward, Vec3(0.f, 1.f, 0.f)).normalized();

        const Uint8* keys = SDL_GetKeyboardState(nullptr);
        float speed = keys[SDL_SCANCODE_LSHIFT] ? 30.f : 10.f;
        if (keys[SDL_SCANCODE_W]) camPos += forward * (speed * dt);
        if (keys[SDL_SCANCODE_S]) camPos -= forward * (speed * dt);
        if (keys[SDL_SCANCODE_A]) camPos -= right * (speed * dt);
        if (keys[SDL_SCANCODE_D]) camPos += right * (speed * dt);
        if (keys[SDL_SCANCODE_Q]) camPos.y -= speed * dt;
        if (keys[SDL_SCANCODE_E]) camPos.y += speed * dt;

        int w, h;
        SDL_GL_GetDrawableSize(app.window, &w, &h);
        float aspect = (float)w / (float)h;
        const float fovDeg = 55.f;
        Mat4 camProj = Mat4::Perspective((double)fovDeg, (double)aspect, kNear, kFar);
        Mat4 camView = Mat4::LookAt(camPos, camPos + forward, Vec3(0.f, 1.f, 0.f));
        Mat4 camVP = camProj * camView;

        for (int i = 0; i < CSM_CASCADES; ++i)
            cascadeMatrices[i] = csmComputeCascadeMatrix(i, aspect, fovDeg, camView, splits,
                                                         lightDir, (float)kShadowSize);

        // --- pass 1: depth from the light, one array layer per cascade ---
        shadowFbo.Bind();
        gl::Renderer::Viewport(0, 0, kShadowSize, kShadowSize);
        gl::Renderer::SetDepthTest(true);
        gl::Renderer::SetCull(gl::CullMode::NONE);
        gl::Renderer::SetPolygonOffset(true, 2.5f, 4.f);

        depthShader.Bind();
        for (int c = 0; c < CSM_CASCADES; ++c)
        {
            shadowFbo.AttachTextureLayer(shadowDepth, gl::Attachment::DEPTH, c);
            gl::Renderer::Clear(false, true);

            Mat4 mvp = cascadeMatrices[c] * planeModel;
            depthShader.SetMat4(depthMvpLoc, csmMat4ptr(mvp));
            planeVao.Bind();
            gl::Renderer::DrawIndexed(gl::RenderPrimitive::TRIANGLES, 6);

            cubeVao.Bind();
            for (int i = 0; i < kNumCubes; ++i)
            {
                mvp = cascadeMatrices[c] * cubeModels[i];
                depthShader.SetMat4(depthMvpLoc, csmMat4ptr(mvp));
                gl::Renderer::DrawIndexed(gl::RenderPrimitive::TRIANGLES, 36);
            }
        }
        gl::Renderer::SetPolygonOffset(false);

        // --- pass 2: camera view with per-fragment cascade selection ---
        gl::Renderer::BindScreen();
        app.BeginFrame();
        gl::Renderer::SetCull(gl::CullMode::BACK);
        gl::Renderer::ClearColor(0.5f, 0.65f, 0.8f, 1.0f);
        gl::Renderer::Clear(true, true);

        sceneShader.Bind();
        sceneShader.SetMat4(sceneVpLoc, csmMat4ptr(camVP));
        sceneShader.SetMat4(sceneViewLoc, csmMat4ptr(camView));
        sceneShader.SetVec3(sceneLightDirLoc, lightDir.x, lightDir.y, lightDir.z);
        sceneShader.SetInt(sceneShowLoc, showCascades ? 1 : 0);
        for (int i = 0; i < CSM_CASCADES; ++i)
        {
            sceneShader.SetMat4(sceneCascadeMatLoc + i, csmMat4ptr(cascadeMatrices[i]));
            sceneShader.SetFloat(sceneSplitLoc + i, splits[i + 1]);
        }
        shadowDepth.Bind(0);

        sceneShader.SetMat4(sceneModelLoc, csmMat4ptr(planeModel));
        sceneShader.SetVec3(sceneColorLoc, 0.75f, 0.75f, 0.75f);
        planeVao.Bind();
        gl::Renderer::DrawIndexed(gl::RenderPrimitive::TRIANGLES, 6);

        cubeVao.Bind();
        for (int i = 0; i < kNumCubes; ++i)
        {
            sceneShader.SetMat4(sceneModelLoc, csmMat4ptr(cubeModels[i]));
            sceneShader.SetVec3(sceneColorLoc, cubeColors[i].x, cubeColors[i].y, cubeColors[i].z);
            gl::Renderer::DrawIndexed(gl::RenderPrimitive::TRIANGLES, 36);
        }

        app.EndFrame(); // gif capture + swap
        ++frame;
        if (maxFrames > 0 && frame >= maxFrames) break;
    }

    printf("frames: %d\n", frame);

    depthShader.Release();
    sceneShader.Release();
    app.Destroy();
    return 0;
}
