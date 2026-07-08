#pragma once

// Verifies MSAA: renders a diagonal-edged triangle into an 8x-multisampled
// FBO (RenderBuffer color + depth) and into a regular single-sample FBO,
// resolves the MSAA one into a plain texture via Renderer::BlitFramebuffer,
// and compares the diagonal edge in both. A non-multisampled render has a
// hard, binary edge (every pixel is either full foreground or full
// background); MSAA must show intermediate blended colors along the edge —
// that's the actual, checkable effect of antialiasing, not just "it didn't
// crash".

#include "test_common.hpp"

static const char* kMsaaVS = R"(#version 430 core
layout(location = 0) in vec2 a_position;
void main()
{
    gl_Position = vec4(a_position, 0.0, 1.0);
}
)";

static const char* kMsaaFS = R"(#version 430 core
out vec4 OutColor;
void main()
{
    OutColor = vec4(1.0, 1.0, 1.0, 1.0);
}
)";

inline int test_msaa_verify(int /*maxFrames*/)
{
    TestApp app;
    if (!app.Create("coregl - MSAA verify", 320, 240)) return 1;

    const int S = 128;

    gl::Shader shader;
    if (!shader.LoadFromString(gl::PipelineStage::VERTEX, kMsaaVS) ||
        !shader.LoadFromString(gl::PipelineStage::FRAGMENT, kMsaaFS) || !shader.Link())
    {
        fprintf(stderr, "shader error: %s\n", shader.GetLog());
        app.Destroy();
        return 1;
    }

    // one big triangle with a diagonal hypotenuse crossing the middle of the
    // target — the interesting edge to check for AA blending
    const float verts[] = {-1.f, -1.f, 1.f, -1.f, -1.f, 1.f};
    gl::Buffer vbo;
    vbo.Allocate(gl::BufferType::ARRAY, verts, sizeof(verts), gl::UsageType::STATIC_DRAW);
    const gl::VertexAttrib layout[] = {{gl::VertexAttribType::FLOAT, 2, 0, false}};
    gl::VertexArray vao;
    vao.AddVertexBuffer(vbo, layout, 1, 2 * sizeof(float));

    // --- MSAA target: renderbuffers, 8 samples ---
    gl::RenderBuffer msColor, msDepth;
    msColor.Allocate(S, S, gl::TextureFormat::RGBA8, 8);
    msDepth.Allocate(S, S, gl::TextureFormat::DEPTH24, 8);
    gl::FrameBuffer msaaFbo;
    msaaFbo.AttachRenderBuffer(msColor, gl::Attachment::COLOR0);
    msaaFbo.AttachRenderBuffer(msDepth, gl::Attachment::DEPTH);
    msaaFbo.SetDrawBuffers();
    if (!msaaFbo.IsComplete())
    {
        fprintf(stderr, "FAIL: MSAA framebuffer incomplete (driver may not support 8 samples)\n");
        app.Destroy();
        return 1;
    }

    // resolve target: regular texture, sampleable/readable afterwards
    gl::Texture resolvedColor;
    resolvedColor.Load2D(nullptr, S, S, gl::TextureFormat::RGBA8);
    gl::FrameBuffer resolveFbo;
    resolveFbo.AttachTexture(resolvedColor, gl::Attachment::COLOR0);
    resolveFbo.SetDrawBuffers();

    // --- non-MSAA reference target: identical scene, single sample ---
    gl::Texture plainColor;
    plainColor.Load2D(nullptr, S, S, gl::TextureFormat::RGBA8);
    gl::FrameBuffer plainFbo;
    plainFbo.AttachTexture(plainColor, gl::Attachment::COLOR0);
    plainFbo.SetDrawBuffers();

    gl::Renderer::SetDepthTest(true);
    shader.Bind();
    vao.Bind();

    // draw into the MSAA target
    msaaFbo.Bind();
    gl::Renderer::Viewport(0, 0, S, S);
    gl::Renderer::ClearColor(0.f, 0.f, 0.f, 1.f);
    gl::Renderer::Clear(true, true);
    gl::Renderer::Draw(gl::RenderPrimitive::TRIANGLES, 3);

    // resolve MSAA -> regular texture (this is the actual antialiasing step:
    // the driver averages the 8 samples of every edge pixel down to 1)
    gl::Renderer::BlitFramebuffer(&msaaFbo, &resolveFbo, 0, 0, S, S, 0, 0, S, S, true, false, false,
                                  gl::TextureFilter::NEAREST);

    // draw the identical scene into the plain (single-sample) target
    plainFbo.Bind();
    gl::Renderer::Viewport(0, 0, S, S);
    gl::Renderer::ClearColor(0.f, 0.f, 0.f, 1.f);
    gl::Renderer::Clear(true, true);
    shader.Bind();
    vao.Bind();
    gl::Renderer::Draw(gl::RenderPrimitive::TRIANGLES, 3);

    gl::u8* resolvedPx = new gl::u8[S * S * 4];
    gl::u8* plainPx = new gl::u8[S * S * 4];

    resolveFbo.Bind();
    gl::Renderer::ReadPixels(0, 0, S, S, resolvedPx);
    plainFbo.Bind();
    gl::Renderer::ReadPixels(0, 0, S, S, plainPx);

    int failed = 0;

    // deep interior of the triangle: both must be solid white
    {
        const gl::u8* r = &resolvedPx[(20 * S + 20) * 4];
        const gl::u8* p = &plainPx[(20 * S + 20) * 4];
        bool ok =
            r[0] == 255 && r[1] == 255 && r[2] == 255 && p[0] == 255 && p[1] == 255 && p[2] == 255;
        printf("interior (both solid white)     resolved=%u plain=%u  %s\n", r[0], p[0],
               ok ? "OK" : "FAIL");
        if (!ok) ++failed;
    }
    // deep exterior: both must be solid black
    {
        const gl::u8* r = &resolvedPx[(S - 10) * S * 4 + (S - 10) * 4];
        const gl::u8* p = &plainPx[(S - 10) * S * 4 + (S - 10) * 4];
        bool ok = r[0] == 0 && r[1] == 0 && r[2] == 0 && p[0] == 0 && p[1] == 0 && p[2] == 0;
        printf("exterior (both solid black)     resolved=%u plain=%u  %s\n", r[0], p[0],
               ok ? "OK" : "FAIL");
        if (!ok) ++failed;
    }

    // the diagonal edge (x + y == S, roughly): scan it and count pixels with
    // an intermediate (blended) value in each version
    // the antialiased band is only ~1 pixel wide, so scan a small
    // neighborhood around the nominal diagonal (x + y == S) per row rather
    // than a single exact pixel, which would easily miss it by rounding
    int plainIntermediate = 0, resolvedIntermediate = 0;
    for (int y = 10; y < S - 10; ++y)
    {
        bool rHit = false, pHit = false;
        for (int x = S - y - 2; x <= S - y + 2; ++x)
        {
            if (x < 0 || x >= S) continue;
            const gl::u8* r = &resolvedPx[(y * S + x) * 4];
            const gl::u8* p = &plainPx[(y * S + x) * 4];
            if (r[0] > 5 && r[0] < 250) rHit = true;
            if (p[0] > 5 && p[0] < 250) pHit = true;
        }
        if (rHit) ++resolvedIntermediate;
        if (pHit) ++plainIntermediate;
    }
    printf("diagonal edge intermediate pixels: plain(no AA)=%d  resolved(MSAA)=%d\n",
           plainIntermediate, resolvedIntermediate);
    bool msaaOk = resolvedIntermediate > plainIntermediate;
    printf("%s\n",
           msaaOk
               ? "OK: MSAA blends the edge, plain rendering does not (as expected)"
               : "FAIL: MSAA resolve did not produce more blended edge pixels than plain render");
    if (!msaaOk) ++failed;

    delete[] resolvedPx;
    delete[] plainPx;

    printf(failed == 0 ? "ALL CHECKS PASSED\n" : "%d CHECKS FAILED\n", failed);

    shader.Release();
    app.Destroy();
    return failed == 0 ? 0 : 1;
}
