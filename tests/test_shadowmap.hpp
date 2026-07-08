#pragma once

// Classic shadow mapping: a plane and a cube, one directional light that
// slowly orbits overhead. Two passes per frame:
//   1. depth-only, from the light's point of view (orthographic — parallel
//      rays), into a depth texture (the "shadow map")
//   2. the normal camera view, where each fragment's position is also
//      projected into light space and compared against the shadow map to
//      decide whether it's occluded
// Uses tests/math/Math.hpp for the camera/light matrices instead of hand-
// rolled helpers (see tutorial_math.hpp for that style).

#include "test_common.hpp"
#include <scene/Math.hpp>

static const char* kShadowDepthVS = R"(#version 430 core
layout(location = 0) in vec3 a_position;
uniform mat4 u_lightMVP;
void main()
{
    gl_Position = u_lightMVP * vec4(a_position, 1.0);
}
)";

// no color output needed — FrameBuffer::SetDrawBuffers() issues
// glDrawBuffers(GL_NONE) when there are no color attachments
static const char* kShadowDepthFS = R"(#version 430 core
void main()
{
}
)";

static const char* kShadowSceneVS = R"(#version 430 core
layout(location = 0) in vec3 a_position;
uniform mat4 u_model;
uniform mat4 u_viewProj;
uniform mat4 u_lightViewProj;
out vec4 v_lightSpacePos;
void main()
{
    vec4 worldPos = u_model * vec4(a_position, 1.0);
    v_lightSpacePos = u_lightViewProj * worldPos;
    gl_Position = u_viewProj * worldPos;
}
)";

static const char* kShadowSceneFS = R"(#version 430 core
in vec4 v_lightSpacePos;
out vec4 OutColor;
uniform sampler2D u_shadowMap;
uniform vec3 u_baseColor;
void main()
{
    vec3 proj = v_lightSpacePos.xyz / v_lightSpacePos.w;
    proj = proj * 0.5 + 0.5; // NDC [-1,1] -> [0,1]

    float shadow = 1.0;
    if (proj.x >= 0.0 && proj.x <= 1.0 && proj.y >= 0.0 && proj.y <= 1.0 && proj.z <= 1.0)
    {
        float shadowMapDepth = texture(u_shadowMap, proj.xy).r;
        float bias = 0.003;
        if (proj.z - bias > shadowMapDepth) shadow = 0.35;
    }
    OutColor = vec4(u_baseColor * shadow, 1.0);
}
)";

// column-major float[16] straight out of Mat4 (already matches Shader::SetMat4)
static const float* mat4ptr(const Mat4& m)
{
    return m.x;
}

inline int test_shadowmap(int maxFrames)
{
    TestApp app;
    if (!app.Create("coregl - shadow map")) return 1;

    gl::Shader depthShader, sceneShader;
    if (!depthShader.LoadFromString(gl::PipelineStage::VERTEX, kShadowDepthVS) ||
        !depthShader.LoadFromString(gl::PipelineStage::FRAGMENT, kShadowDepthFS) ||
        !depthShader.Link() ||
        !sceneShader.LoadFromString(gl::PipelineStage::VERTEX, kShadowSceneVS) ||
        !sceneShader.LoadFromString(gl::PipelineStage::FRAGMENT, kShadowSceneFS) ||
        !sceneShader.Link())
    {
        fprintf(stderr, "shader error: %s%s\n", depthShader.GetLog(), sceneShader.GetLog());
        app.Destroy();
        return 1;
    }

    // --- ground plane: one quad, 8x8 units, XZ plane at y=0 ---
    const float planeHalf = 4.0f;
    const float planeVerts[] = {
        -planeHalf, 0.f, -planeHalf, planeHalf,  0.f, -planeHalf,
        planeHalf,  0.f, planeHalf,  -planeHalf, 0.f, planeHalf,
    };
    // wound CCW as seen by this test's camera (eye above +Z, looking down
    // at the origin) — verified with an isolated NONE/BACK/FRONT pixel-count
    // check (the same technique as test_winding_verify.hpp): {0,1,2,0,2,3}
    // is back-facing here and vanishes under normal backface culling
    const gl::u16 planeIndices[] = {0, 2, 1, 0, 3, 2};
    gl::Buffer planeVbo, planeIbo;
    planeVbo.Allocate(gl::BufferType::ARRAY, planeVerts, sizeof(planeVerts),
                      gl::UsageType::STATIC_DRAW);
    planeIbo.Allocate(gl::BufferType::ELEMENT_ARRAY, planeIndices, sizeof(planeIndices),
                      gl::UsageType::STATIC_DRAW);
    const gl::VertexAttrib posLayout[] = {{gl::VertexAttribType::FLOAT, 3, 0, false}};
    gl::VertexArray planeVao;
    planeVao.AddVertexBuffer(planeVbo, posLayout, 1, 3 * sizeof(float));
    planeVao.SetIndexBuffer(planeIbo, gl::VertexAttribType::USHORT);

    // --- unit cube, sitting on the plane (y in [0,1]) ---
    // clang-format off
    const float cubeVerts[] = {
        -0.5f,0.f,-0.5f,  -0.5f,1.f,-0.5f,   0.5f,1.f,-0.5f,   0.5f,0.f,-0.5f, // -Z
        -0.5f,0.f, 0.5f,   0.5f,0.f, 0.5f,   0.5f,1.f, 0.5f,  -0.5f,1.f, 0.5f, // +Z
        -0.5f,0.f,-0.5f,  -0.5f,0.f, 0.5f,  -0.5f,1.f, 0.5f,  -0.5f,1.f,-0.5f, // -X
         0.5f,0.f,-0.5f,   0.5f,1.f,-0.5f,   0.5f,1.f, 0.5f,   0.5f,0.f, 0.5f, // +X
        -0.5f,0.f,-0.5f,   0.5f,0.f,-0.5f,   0.5f,0.f, 0.5f,  -0.5f,0.f, 0.5f, // -Y
        -0.5f,1.f,-0.5f,  -0.5f,1.f, 0.5f,   0.5f,1.f, 0.5f,   0.5f,1.f,-0.5f, // +Y
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
    cubeVao.AddVertexBuffer(cubeVbo, posLayout, 1, 3 * sizeof(float));
    cubeVao.SetIndexBuffer(cubeIbo, gl::VertexAttribType::USHORT);

    // --- shadow map: depth-only offscreen target ---
    const int kShadowSize = 1024;
    gl::Texture shadowDepth;
    shadowDepth.LoadDepth(kShadowSize, kShadowSize);
    shadowDepth.SetWrap(gl::TextureWrap::CLAMP_TO_EDGE, gl::TextureWrap::CLAMP_TO_EDGE);
    gl::FrameBuffer shadowFbo;
    shadowFbo.AttachTexture(shadowDepth, gl::Attachment::DEPTH);
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
    const gl::i32 sceneLightVpLoc = sceneShader.GetLocation("u_lightViewProj");
    const gl::i32 sceneColorLoc = sceneShader.GetLocation("u_baseColor");
    sceneShader.SetInt("u_shadowMap", 0);

    Mat4 planeModel = Mat4::Translate(0.f, 0.f, 0.f);
    Mat4 cubeModel = Mat4::Translate(0.f, 0.f, 0.f);

    int frame = 0;
    while (app.PollEvents())
    {
        float t = (float)frame / 90.0f;

        // directional light slowly orbiting overhead
        Vec3 lightDir = Vec3(0.6f * cosf(t), -1.0f, 0.6f * sinf(t)).normalized();
        Vec3 lightPos = lightDir * -10.0f;
        Mat4 lightView = Mat4::LookAt(lightPos, Vec3(0.f, 0.f, 0.f), Vec3(0.f, 1.f, 0.f));
        Mat4 lightProj = Mat4::Ortho(-6.f, 6.f, -6.f, 6.f, 0.1f, 30.f);
        Mat4 lightVP = lightProj * lightView;

        // --- pass 1: depth from the light ---
        shadowFbo.Bind();
        gl::Renderer::Viewport(0, 0, kShadowSize, kShadowSize);
        gl::Renderer::SetDepthTest(true);
        gl::Renderer::SetCull(gl::CullMode::NONE);
        gl::Renderer::SetPolygonOffset(true, 2.5f, 4.f); // reduce shadow acne
        gl::Renderer::Clear(false, true);

        depthShader.Bind();
        Mat4 lightMvpPlane = lightVP * planeModel;
        depthShader.SetMat4(depthMvpLoc, mat4ptr(lightMvpPlane));
        planeVao.Bind();
        gl::Renderer::DrawIndexed(gl::RenderPrimitive::TRIANGLES, 6);

        Mat4 lightMvpCube = lightVP * cubeModel;
        depthShader.SetMat4(depthMvpLoc, mat4ptr(lightMvpCube));
        cubeVao.Bind();
        gl::Renderer::DrawIndexed(gl::RenderPrimitive::TRIANGLES, 36);

        gl::Renderer::SetPolygonOffset(false);

        // --- pass 2: camera view, shadowed ---
        gl::Renderer::BindScreen();
        app.BeginFrame();
        int w, h;
        SDL_GL_GetDrawableSize(app.window, &w, &h);
        gl::Renderer::SetCull(gl::CullMode::BACK);
        gl::Renderer::ClearColor(0.5f, 0.65f, 0.8f, 1.0f);
        gl::Renderer::Clear(true, true);

        Mat4 camProj = Mat4::Perspective(55.0, (double)w / (double)h, 0.1, 100.0);
        Mat4 camView = Mat4::LookAt(Vec3(0.f, 3.f, 6.f), Vec3(0.f, 0.5f, 0.f), Vec3(0.f, 1.f, 0.f));
        Mat4 camVP = camProj * camView;
        if (frame == 0)
            printf("DIAG w=%d h=%d aspect=%f camProj.x[0]=%f camProj.x[5]=%f\n", w, h,
                   (double)w / (double)h, camProj.x[0], camProj.x[5]);

        sceneShader.Bind();
        sceneShader.SetMat4(sceneVpLoc, mat4ptr(camVP));
        sceneShader.SetMat4(sceneLightVpLoc, mat4ptr(lightVP));
        shadowDepth.Bind(0);

        sceneShader.SetMat4(sceneModelLoc, mat4ptr(planeModel));
        sceneShader.SetVec3(sceneColorLoc, 0.75f, 0.75f, 0.75f);
        planeVao.Bind();

        if (frame == 0)
        {
            // one-shot diagnostic: a completely FRESH VAO/VBO/IBO, created
            // right here (after pass 1 already ran), same vertex/index data,
            // same shader — isolates whether planeVao/planeVbo specifically
            // are contaminated by something, vs. a brand new object
            gl::Buffer freshVbo, freshIbo;
            freshVbo.Allocate(gl::BufferType::ARRAY, planeVerts, sizeof(planeVerts),
                              gl::UsageType::STATIC_DRAW);
            freshIbo.Allocate(gl::BufferType::ELEMENT_ARRAY, planeIndices, sizeof(planeIndices),
                              gl::UsageType::STATIC_DRAW);
            gl::VertexArray freshVao;
            freshVao.AddVertexBuffer(freshVbo, posLayout, 1, 3 * sizeof(float));
            freshVao.SetIndexBuffer(freshIbo, gl::VertexAttribType::USHORT);
            freshVao.Bind();

            gl::u8* dbg = new gl::u8[w * h * 4];
            const gl::CullMode modes[3] = {gl::CullMode::NONE, gl::CullMode::BACK,
                                           gl::CullMode::FRONT};
            const char* names[3] = {"NONE", "BACK", "FRONT"};
            for (int m = 0; m < 3; ++m)
            {
                gl::Renderer::SetCull(modes[m]);
                gl::Renderer::Clear(true, true);
                gl::Renderer::DrawIndexed(gl::RenderPrimitive::TRIANGLES, 6);
                gl::Renderer::ReadPixels(0, 0, w, h, dbg);
                int count = 0;
                for (int i = 0; i < w * h; ++i)
                {
                    gl::u8 r = dbg[i * 4], g = dbg[i * 4 + 1], b = dbg[i * 4 + 2];
                    bool isSky = b > r + 15; // sky = (0.5,0.65,0.8); floor = (0.75,0.75,0.75)*shadow
                    if (!isSky) ++count;
                }
                printf("DIAG FRESH-VAO plane-only cull=%s non-sky pixels=%d\n", names[m], count);
            }
            delete[] dbg;
            freshVao.Release();
            freshVbo.Release();
            freshIbo.Release();

            planeVao.Bind();
            gl::Renderer::SetCull(gl::CullMode::BACK);
            gl::Renderer::Clear(true, true);
        }
        gl::Renderer::DrawIndexed(gl::RenderPrimitive::TRIANGLES, 6);

        sceneShader.SetMat4(sceneModelLoc, mat4ptr(cubeModel));
        sceneShader.SetVec3(sceneColorLoc, 0.85f, 0.35f, 0.25f);
        cubeVao.Bind();
        gl::Renderer::DrawIndexed(gl::RenderPrimitive::TRIANGLES, 36);

        app.EndFrame();
        ++frame;
        if (maxFrames > 0 && frame >= maxFrames) break;
    }

    printf("frames: %d\n", frame);

    depthShader.Release();
    sceneShader.Release();
    app.Destroy();
    return 0;
}
