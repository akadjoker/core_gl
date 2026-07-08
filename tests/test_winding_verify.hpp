#pragma once

// Verifies triangle winding on every solid shape (tutorial cube + all of
// Batch's solids) by rendering each one twice per angle — once with
// CullMode::NONE, once with CullMode::BACK — and comparing lit pixel counts.
// For a correctly wound, closed convex solid, depth testing alone already
// hides every back face, so culling must never change the picture: any
// mismatch means at least one face has reversed winding and is being
// discarded by backface culling even though it should be visible — a "hole"
// from some viewing angle, exactly the class of bug that inconsistent
// winding causes. This is a stronger check than eyeballing a rotation.

#include "test_common.hpp"
#include "tutorial_math.hpp"

static const char* kWindingVS = R"(#version 430 core
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

static const char* kWindingFS = R"(#version 430 core
in vec3 v_color;
out vec4 OutColor;
void main()
{
    OutColor = vec4(v_color, 1.0);
}
)";

// counts pixels that differ from the clear color (0,0,0) — i.e. "lit" pixels
static int countLitPixels(const gl::u8* px, int w, int h)
{
    int count = 0;
    for (int i = 0; i < w * h; ++i)
    {
        const gl::u8* p = &px[i * 4];
        if (p[0] || p[1] || p[2]) ++count;
    }
    return count;
}

// renders with the given culling mode from `angle` (deg, around Y) tilted 20
// degrees, returns lit pixel count
template <typename DrawFn>
static int renderWinding(int w, int h, gl::u8* px, float angleDeg, gl::CullMode cull, DrawFn draw)
{
    gl::Renderer::SetCull(cull);
    gl::Renderer::Clear(true, true);

    float proj[16], view[16], tilt[16], pv[16];
    tut_mat4_perspective(proj, 55.f, (float)w / (float)h, 0.1f, 100.f);
    tut_mat4_translate(view, 0.f, 0.f, -3.5f);
    tut_mat4_rotate_xy(tilt, 20.f, angleDeg);
    tut_mat4_mul(view, view, tilt);
    tut_mat4_mul(pv, proj, view);

    draw(pv);

    gl::Renderer::ReadPixels(0, 0, w, h, px);
    return countLitPixels(px, w, h);
}

// C++11 has no generic lambdas, so this stays a free template function; the
// draw callback itself is an ordinary typed lambda (const float* pv).
template <typename DrawFn>
static void checkShape(const char* name, int S, gl::u8* px, const float* angles, int angleCount,
                       int& failed, DrawFn draw)
{
    int mismatches = 0;
    for (int i = 0; i < angleCount; ++i)
    {
        int withNone = renderWinding(S, S, px, angles[i], gl::CullMode::NONE, draw);
        int withBack = renderWinding(S, S, px, angles[i], gl::CullMode::BACK, draw);
        if (withNone != withBack)
        {
            printf("  [%s] angle %.0f: NONE=%d BACK=%d MISMATCH (hole from bad winding)\n", name,
                   angles[i], withNone, withBack);
            ++mismatches;
        }
    }
    bool ok = mismatches == 0;
    printf("%-28s %s\n", name, ok ? "OK (culling never changes the picture)" : "FAIL");
    if (!ok) ++failed;
}

inline int test_winding_verify(int /*maxFrames*/)
{
    TestApp app;
    if (!app.Create("coregl - winding verify", 320, 240)) return 1;

    const int S = 256;
    gl::Texture color;
    color.Load2D(nullptr, S, S, gl::TextureFormat::RGBA8);
    gl::Texture depth;
    depth.LoadDepth(S, S);
    gl::FrameBuffer fbo;
    fbo.AttachTexture(color, gl::Attachment::COLOR0);
    fbo.AttachTexture(depth, gl::Attachment::DEPTH);
    fbo.SetDrawBuffers();
    if (!fbo.IsComplete())
    {
        fprintf(stderr, "FAIL: framebuffer incomplete\n");
        app.Destroy();
        return 1;
    }
    fbo.Bind();
    gl::Renderer::Viewport(0, 0, S, S);
    gl::Renderer::SetDepthTest(true);
    gl::Renderer::ClearColor(0.f, 0.f, 0.f, 1.f);

    gl::u8* px = new gl::u8[S * S * 4];
    int failed = 0;
    const float angles[] = {0.f, 30.f, 60.f, 90.f, 120.f, 150.f, 200.f, 260.f, 310.f};
    const int angleCount = (int)(sizeof(angles) / sizeof(angles[0]));

    // --- 1) the tutorial cube (raw VBO/IBO) ---
    {
        gl::Shader shader;
        shader.LoadFromString(gl::PipelineStage::VERTEX, kWindingVS);
        shader.LoadFromString(gl::PipelineStage::FRAGMENT, kWindingFS);
        shader.Link();
        const gl::i32 mvpLoc = shader.GetLocation("u_mvp");

        // clang-format off
        const float verts[] = {
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
        gl::u16 indices[36];
        for (int face = 0; face < 6; ++face)
        {
            gl::u16 base = (gl::u16)(face * 4);
            gl::u16* idx = &indices[face * 6];
            idx[0] = base;
            idx[1] = base + 1;
            idx[2] = base + 2;
            idx[3] = base;
            idx[4] = base + 2;
            idx[5] = base + 3;
        }
        gl::Buffer vbo, ibo;
        vbo.Allocate(gl::BufferType::ARRAY, verts, sizeof(verts), gl::UsageType::STATIC_DRAW);
        ibo.Allocate(gl::BufferType::ELEMENT_ARRAY, indices, sizeof(indices),
                     gl::UsageType::STATIC_DRAW);
        const gl::VertexAttrib layout[] = {
            {gl::VertexAttribType::FLOAT, 3, 0, false},
            {gl::VertexAttribType::FLOAT, 3, 0, false},
        };
        gl::VertexArray vao;
        vao.AddVertexBuffer(vbo, layout, 2, 6 * sizeof(float));
        vao.SetIndexBuffer(ibo, gl::VertexAttribType::USHORT);

        checkShape("tutorial cube (raw VBO/IBO)", S, px, angles, angleCount, failed,
                   [&](const float* pv)
                   {
                       shader.Bind();
                       shader.SetMat4(mvpLoc, pv);
                       vao.Bind();
                       gl::Renderer::DrawIndexed(gl::RenderPrimitive::TRIANGLES, 36);
                   });

        shader.Release();
        vbo.Release();
        ibo.Release();
        vao.Release();
    }

    // --- 2) every Batch solid shape ---
    {
        gl::Batch batch;
        batch.Init(4096);

        checkShape("Batch::Cube", S, px, angles, angleCount, failed,
                   [&](const float* pv)
                   {
                       batch.SetProjection(pv);
                       batch.SetColor(255, 120, 80, 255);
                       batch.Cube(0, 0, 0, 1.4f, 1.4f, 1.4f);
                       batch.Render();
                   });
        checkShape("Batch::Sphere", S, px, angles, angleCount, failed,
                   [&](const float* pv)
                   {
                       batch.SetProjection(pv);
                       batch.SetColor(80, 200, 255, 255);
                       batch.Sphere(0, 0, 0, 0.9f, 10, 20);
                       batch.Render();
                   });
        checkShape("Batch::Cylinder", S, px, angles, angleCount, failed,
                   [&](const float* pv)
                   {
                       batch.SetProjection(pv);
                       batch.SetColor(160, 255, 120, 255);
                       batch.Cylinder(0, 0, 0, 0.7f, 1.6f, 20);
                       batch.Render();
                   });
        checkShape("Batch::Capsule", S, px, angles, angleCount, failed,
                   [&](const float* pv)
                   {
                       batch.SetProjection(pv);
                       batch.SetColor(255, 220, 90, 255);
                       batch.Capsule(0, 0, 0, 0.5f, 2.0f, 6, 20);
                       batch.Render();
                   });

        batch.Release();
    }

    delete[] px;
    printf(failed == 0 ? "ALL SHAPES WOUND CORRECTLY\n" : "%d SHAPE(S) HAVE WINDING BUGS\n",
           failed);

    app.Destroy();
    return failed == 0 ? 0 : 1;
}
