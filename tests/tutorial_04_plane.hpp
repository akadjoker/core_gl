#pragma once

// TUTORIAL 4 — Textured Ground Plane
//
// Combines tutorials 2 and 3: a tiled textured plane sitting under the
// rotating cube, sharing one camera. New idea: TextureWrap::REPEAT — UV
// coordinates that go past 1.0 repeat the texture instead of clamping,
// which is how tiling ground/wall textures work. The plane's UVs go 0..6,
// not 0..1, so the checker texture repeats 6 times across it.

#include "test_common.hpp"
#include "tutorial_math.hpp"

static const char* kTutorialPlaneVS = R"(#version 430 core
layout(location = 0) in vec3 a_position;
layout(location = 1) in vec2 a_uv;
uniform mat4 u_mvp;
out vec2 v_uv;
void main()
{
    gl_Position = u_mvp * vec4(a_position, 1.0);
    v_uv = a_uv;
}
)";

static const char* kTutorialPlaneFS = R"(#version 430 core
in vec2 v_uv;
out vec4 OutColor;
uniform sampler2D u_tex;
void main()
{
    OutColor = texture(u_tex, v_uv);
}
)";

// same cube shader as tutorial 3 (flat per-vertex color, no texture)
static const char* kTutorialCubeVS2 = R"(#version 430 core
layout(location = 0) in vec3 a_position;
layout(location = 1) in vec3 a_color;
uniform mat4 u_mvp;
out vec3 v_color;
void main()
{
    gl_Position = u_mvp * vec4(a_position, 1.0);
    v_color = a_color;
}
)";

static const char* kTutorialCubeFS2 = R"(#version 430 core
in vec3 v_color;
out vec4 OutColor;
void main()
{
    OutColor = vec4(v_color, 1.0);
}
)";

inline int tutorial_04_plane(int maxFrames)
{
    TestApp app;
    if (!app.Create("tutorial 04 - plane")) return 1;

    gl::Shader planeShader, cubeShader;
    if (!planeShader.LoadFromString(gl::PipelineStage::VERTEX, kTutorialPlaneVS) ||
        !planeShader.LoadFromString(gl::PipelineStage::FRAGMENT, kTutorialPlaneFS) ||
        !planeShader.Link() ||
        !cubeShader.LoadFromString(gl::PipelineStage::VERTEX, kTutorialCubeVS2) ||
        !cubeShader.LoadFromString(gl::PipelineStage::FRAGMENT, kTutorialCubeFS2) ||
        !cubeShader.Link())
    {
        fprintf(stderr, "shader error: %s%s\n", planeShader.GetLog(), cubeShader.GetLog());
        app.Destroy();
        return 1;
    }

    // --- ground plane: one quad in the XZ plane, y = 0, UVs tiled 6x ---
    const float kTiles = 6.0f;
    const float kHalfSize = 6.0f;
    const float planeVerts[] = {
        // x,            y,     z,             u,       v
        -kHalfSize, 0.0f, -kHalfSize, 0.0f,   0.0f,   kHalfSize,  0.0f, -kHalfSize, kTiles, 0.0f,
        kHalfSize,  0.0f, kHalfSize,  kTiles, kTiles, -kHalfSize, 0.0f, kHalfSize,  0.0f,   kTiles,
    };
    const gl::u16 planeIndices[] = {0, 1, 2, 0, 2, 3};

    gl::Buffer planeVbo, planeIbo;
    planeVbo.Allocate(gl::BufferType::ARRAY, planeVerts, sizeof(planeVerts),
                      gl::UsageType::STATIC_DRAW);
    planeIbo.Allocate(gl::BufferType::ELEMENT_ARRAY, planeIndices, sizeof(planeIndices),
                      gl::UsageType::STATIC_DRAW);
    const gl::VertexAttrib planeLayout[] = {
        {gl::VertexAttribType::FLOAT, 3, 0, false}, // a_position
        {gl::VertexAttribType::FLOAT, 2, 0, false}, // a_uv
    };
    gl::VertexArray planeVao;
    planeVao.AddVertexBuffer(planeVbo, planeLayout, 2, 5 * sizeof(float));
    planeVao.SetIndexBuffer(planeIbo, gl::VertexAttribType::USHORT);

    // 16x16 procedural checkerboard — no file loading needed
    const int kTexSize = 16;
    gl::u8 checker[kTexSize * kTexSize * 4];
    for (int y = 0; y < kTexSize; ++y)
        for (int x = 0; x < kTexSize; ++x)
        {
            gl::u8 c = (((x >> 3) + (y >> 3)) & 1) ? 210 : 90;
            gl::u8* p = &checker[(y * kTexSize + x) * 4];
            p[0] = p[1] = p[2] = c;
            p[3] = 255;
        }
    gl::Texture groundTex;
    groundTex.Load2D(checker, kTexSize, kTexSize, gl::TextureFormat::RGBA8);
    groundTex.SetFilter(gl::TextureFilter::NEAREST, gl::TextureFilter::NEAREST);
    groundTex.SetWrap(gl::TextureWrap::REPEAT, gl::TextureWrap::REPEAT); // tiling happens here
    planeShader.SetInt("u_tex", 0);

    // --- the cube from tutorial 3, unchanged, sitting above the plane ---
    // (winding: every face CCW as seen from outside, see tutorial 3's comment)
    // clang-format off
    const float cubeVerts[] = {
        -0.5f,-0.5f,-0.5f, 1,0,0,  -0.5f, 0.5f,-0.5f, 1,0,0,
         0.5f, 0.5f,-0.5f, 1,0,0,   0.5f,-0.5f,-0.5f, 1,0,0,
        -0.5f,-0.5f, 0.5f, 0,1,0,   0.5f,-0.5f, 0.5f, 0,1,0,
         0.5f, 0.5f, 0.5f, 0,1,0,  -0.5f, 0.5f, 0.5f, 0,1,0,
        -0.5f,-0.5f,-0.5f, 0,0,1,  -0.5f,-0.5f, 0.5f, 0,0,1,
        -0.5f, 0.5f, 0.5f, 0,0,1,  -0.5f, 0.5f,-0.5f, 0,0,1,
         0.5f,-0.5f,-0.5f, 1,1,0,   0.5f, 0.5f,-0.5f, 1,1,0,
         0.5f, 0.5f, 0.5f, 1,1,0,   0.5f,-0.5f, 0.5f, 1,1,0,
        -0.5f,-0.5f,-0.5f, 1,0,1,   0.5f,-0.5f,-0.5f, 1,0,1,
         0.5f,-0.5f, 0.5f, 1,0,1,  -0.5f,-0.5f, 0.5f, 1,0,1,
        -0.5f, 0.5f,-0.5f, 0,1,1,  -0.5f, 0.5f, 0.5f, 0,1,1,
         0.5f, 0.5f, 0.5f, 0,1,1,   0.5f, 0.5f,-0.5f, 0,1,1,
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
    const gl::VertexAttrib cubeLayout[] = {
        {gl::VertexAttribType::FLOAT, 3, 0, false},
        {gl::VertexAttribType::FLOAT, 3, 0, false},
    };
    gl::VertexArray cubeVao;
    cubeVao.AddVertexBuffer(cubeVbo, cubeLayout, 2, 6 * sizeof(float));
    cubeVao.SetIndexBuffer(cubeIbo, gl::VertexAttribType::USHORT);

    const gl::i32 planeMvpLoc = planeShader.GetLocation("u_mvp");
    const gl::i32 cubeMvpLoc = cubeShader.GetLocation("u_mvp");

    gl::Renderer::ClearColor(0.5f, 0.65f, 0.8f, 1.0f);
    gl::Renderer::SetDepthTest(true);
    gl::Renderer::SetCull(gl::CullMode::BACK);

    int frame = 0;
    while (app.PollEvents())
    {
        app.BeginFrame();
        int w, h;
        SDL_GL_GetDrawableSize(app.window, &w, &h);
        gl::Renderer::Clear(true, true);

        float t = (float)frame / 60.0f;

        // one shared camera for both objects
        float proj[16], view[16], tilt[16], pv[16];
        tut_mat4_perspective(proj, 55.f, (float)w / (float)h, 0.1f, 100.f);
        tut_mat4_translate(view, 0.f, -1.0f, -8.f);
        tut_mat4_rotate_xy(tilt, 20.f, t * 15.f); // slow orbit around the scene
        tut_mat4_mul(view, view, tilt);
        tut_mat4_mul(pv, proj, view);

        // ground: model matrix is identity (the plane's own vertices are
        // already in world space), so mvp = pv directly
        planeShader.Bind();
        planeShader.SetMat4(planeMvpLoc, pv);
        groundTex.Bind(0);
        planeVao.Bind();
        gl::Renderer::DrawIndexed(gl::RenderPrimitive::TRIANGLES, 6);

        // cube: floats above the ground, spinning on its own
        float model[16], mv[16], mvp[16];
        tut_mat4_rotate_xy(model, 0.f, t * 50.f);
        model[13] = 0.9f; // lift it above the plane (translation row, y)
        tut_mat4_mul(mv, view, model);
        tut_mat4_mul(mvp, proj, mv);

        cubeShader.Bind();
        cubeShader.SetMat4(cubeMvpLoc, mvp);
        cubeVao.Bind();
        gl::Renderer::DrawIndexed(gl::RenderPrimitive::TRIANGLES, 36);

        app.EndFrame();
        ++frame;
        if (maxFrames > 0 && frame >= maxFrames) break;
    }

    groundTex.Release();
    planeShader.Release();
    planeVbo.Release();
    planeIbo.Release();
    planeVao.Release();
    cubeShader.Release();
    cubeVbo.Release();
    cubeIbo.Release();
    cubeVao.Release();
    app.Destroy();
    return 0;
}
