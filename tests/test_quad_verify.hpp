#pragma once

// Verifies Renderer::DrawQuad(): draws two positioned, differently-sized
// rects into the SAME 256x256 offscreen target, in the SAME viewport call,
// each with its own u_rect uniform — no Viewport()/Scissor() juggling
// between them. Confirms the geometry (not global GL state) is what places
// each quad, so multiple positioned draws compose freely in one target
// (split-screen, HUD panels, picture-in-picture...).

#include "test_common.hpp"

inline int test_quad_verify(int /*maxFrames*/)
{
    TestApp app;
    if (!app.Create("coregl - quad verify", 320, 240)) return 1;

    const int S = 256;
    gl::Texture color;
    color.Load2D(nullptr, S, S, gl::TextureFormat::RGBA8);
    gl::FrameBuffer fbo;
    fbo.AttachTexture(color, gl::Attachment::COLOR0);
    fbo.SetDrawBuffers();
    if (!fbo.IsComplete())
    {
        fprintf(stderr, "FAIL: framebuffer incomplete\n");
        app.Destroy();
        return 1;
    }

    static const char* kSolidFS = R"(#version 430 core
in vec2 v_uv;
out vec4 OutColor;
uniform vec3 u_color;
void main()
{
    OutColor = vec4(u_color, 1.0);
}
)";

    gl::Shader shader;
    if (!shader.LoadFromString(gl::PipelineStage::VERTEX, gl::Renderer::QuadShaderSource()) ||
        !shader.LoadFromString(gl::PipelineStage::FRAGMENT, kSolidFS) || !shader.Link())
    {
        fprintf(stderr, "FAIL: shader error: %s\n", shader.GetLog());
        app.Destroy();
        return 1;
    }

    const gl::i32 rectLoc = shader.GetLocation("u_rect");
    const gl::i32 sizeLoc = shader.GetLocation("u_targetSize");
    const gl::i32 colorLoc = shader.GetLocation("u_color");

    fbo.Bind();
    gl::Renderer::Viewport(0, 0, S, S); // set ONCE — never touched again below
    gl::Renderer::SetDepthTest(false);
    gl::Renderer::SetBlend(false);
    gl::Renderer::ClearColor(0.f, 0.f, 0.f, 1.f);
    gl::Renderer::Clear(true, false);

    shader.Bind();
    shader.SetVec2(sizeLoc, (float)S, (float)S);

    // rect A: top-left area [16,16)..[80,64) — red
    shader.SetVec4(rectLoc, 16.f, 16.f, 64.f, 48.f);
    shader.SetVec3(colorLoc, 1.f, 0.f, 0.f);
    gl::Renderer::DrawQuad();

    // rect B: bottom-right area, same viewport, no Viewport() call in between — green
    shader.SetVec4(rectLoc, 150.f, 150.f, 90.f, 70.f);
    shader.SetVec3(colorLoc, 0.f, 1.f, 0.f);
    gl::Renderer::DrawQuad();

    gl::u8* px = new gl::u8[S * S * 4];
    gl::Renderer::ReadPixels(0, 0, S, S, px);

    // note: framebuffer readback is bottom-up (GL y=0 at the bottom), while
    // u_rect/u_targetSize use top-left pixel origin — so a rect at pixel
    // row `py` (top-left convention) reads back at row `S - 1 - py`.
    struct Check
    {
        int x, y;
        gl::u8 r, g, b;
        const char* what;
    };
    const Check checks[] = {
        {40, S - 1 - 30, 255, 0, 0, "rect A interior (red)"},
        {10, S - 1 - 10, 0, 0, 0, "outside both rects (background)"},
        {190, S - 1 - 180, 0, 255, 0, "rect B interior (green)"},
        {40, S - 1 - 180, 0, 0, 0, "between the two rects (background)"},
    };

    int failed = 0;
    for (const Check& c : checks)
    {
        const gl::u8* p = &px[(c.y * S + c.x) * 4];
        bool ok = (p[0] == c.r && p[1] == c.g && p[2] == c.b);
        printf("%-32s (%3d,%3d) got (%3u,%3u,%3u) expected (%3u,%3u,%3u)  %s\n", c.what, c.x, c.y,
               p[0], p[1], p[2], c.r, c.g, c.b, ok ? "OK" : "FAIL");
        if (!ok) ++failed;
    }
    delete[] px;

    printf(failed == 0 ? "ALL CHECKS PASSED\n" : "%d CHECKS FAILED\n", failed);

    shader.Release();
    app.Destroy();
    return failed == 0 ? 0 : 1;
}
