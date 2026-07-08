#pragma once

// Verifies Renderer::BlitFramebuffer(): renders a 2x2 grid of solid colors
// into a small source framebuffer, blits it (scaled up) into a larger
// destination framebuffer, and reads back the destination to confirm each
// quadrant landed in the right place with the right color. Also checks a
// partial-rect blit (only one quadrant copied) leaves the rest of the
// destination untouched.

#include "test_common.hpp"

inline int test_blit_verify(int /*maxFrames*/)
{
    TestApp app;
    if (!app.Create("coregl - blit verify", 320, 240)) return 1;

    const int srcSize = 64;
    const int dstSize = 256;

    gl::Texture srcColor;
    srcColor.Load2D(nullptr, srcSize, srcSize, gl::TextureFormat::RGBA8);
    gl::FrameBuffer srcFbo;
    srcFbo.AttachTexture(srcColor, gl::Attachment::COLOR0);
    srcFbo.SetDrawBuffers();
    if (!srcFbo.IsComplete())
    {
        fprintf(stderr, "FAIL: src framebuffer incomplete\n");
        app.Destroy();
        return 1;
    }

    gl::Texture dstColor;
    dstColor.Load2D(nullptr, dstSize, dstSize, gl::TextureFormat::RGBA8);
    gl::FrameBuffer dstFbo;
    dstFbo.AttachTexture(dstColor, gl::Attachment::COLOR0);
    dstFbo.SetDrawBuffers();
    if (!dstFbo.IsComplete())
    {
        fprintf(stderr, "FAIL: dst framebuffer incomplete\n");
        app.Destroy();
        return 1;
    }

    gl::Batch batch;
    if (!batch.Init(1024))
    {
        fprintf(stderr, "batch init failed\n");
        app.Destroy();
        return 1;
    }

    // 2x2 grid: red(bottom-left) green(bottom-right) blue(top-left) yellow(top-right),
    // in framebuffer pixel space (y=0 at the bottom, OpenGL convention)
    float proj[16] = {0};
    proj[0] = 2.f / (float)srcSize;
    proj[5] = 2.f / (float)srcSize;
    proj[10] = 1.f;
    proj[12] = -1.f;
    proj[13] = -1.f;
    proj[15] = 1.f;

    srcFbo.Bind();
    gl::Renderer::Viewport(0, 0, srcSize, srcSize);
    gl::Renderer::SetDepthTest(false);
    gl::Renderer::SetBlend(false);
    gl::Renderer::ClearColor(0.f, 0.f, 0.f, 1.f);
    gl::Renderer::Clear(true, false);

    batch.SetProjection(proj);
    const int half = srcSize / 2;
    batch.SetColor(255, 0, 0, 255);
    batch.Rect(0, 0, half, half, true); // bottom-left
    batch.SetColor(0, 255, 0, 255);
    batch.Rect(half, 0, half, half, true); // bottom-right
    batch.SetColor(0, 0, 255, 255);
    batch.Rect(0, half, half, half, true); // top-left
    batch.SetColor(255, 255, 0, 255);
    batch.Rect(half, half, half, half, true); // top-right
    batch.Render();

    // --- test 1: full blit, scaled up 4x ---
    dstFbo.Bind();
    gl::Renderer::Viewport(0, 0, dstSize, dstSize);
    gl::Renderer::ClearColor(0.f, 0.f, 0.f, 1.f);
    gl::Renderer::Clear(true, false);
    gl::Renderer::BlitFramebuffer(&srcFbo, &dstFbo, 0, 0, srcSize, srcSize, 0, 0, dstSize, dstSize,
                                  true, false, false, gl::TextureFilter::NEAREST);

    gl::u8* px = new gl::u8[dstSize * dstSize * 4];
    gl::Renderer::ReadPixels(0, 0, dstSize, dstSize, px);

    struct Check
    {
        int x, y;
        gl::u8 r, g, b;
        const char* what;
    };
    const Check checks[] = {
        {64, 64, 255, 0, 0, "scaled blit: bottom-left quadrant (red)"},
        {192, 64, 0, 255, 0, "scaled blit: bottom-right quadrant (green)"},
        {64, 192, 0, 0, 255, "scaled blit: top-left quadrant (blue)"},
        {192, 192, 255, 255, 0, "scaled blit: top-right quadrant (yellow)"},
    };
    int failed = 0;
    for (const Check& c : checks)
    {
        const gl::u8* p = &px[(c.y * dstSize + c.x) * 4];
        bool ok = (p[0] == c.r && p[1] == c.g && p[2] == c.b);
        printf("%-42s got (%3u,%3u,%3u) expected (%3u,%3u,%3u)  %s\n", c.what, p[0], p[1], p[2],
               c.r, c.g, c.b, ok ? "OK" : "FAIL");
        if (!ok) ++failed;
    }

    // --- test 2: partial-rect blit — only the src's red quadrant, into the
    // dst's top-right quadrant; the rest of dst must remain whatever was
    // there before (still the scaled copy from test 1: green at that spot)
    gl::Renderer::BlitFramebuffer(&srcFbo, &dstFbo, 0, 0, half, half, // src: bottom-left (red)
                                  dstSize / 2, dstSize / 2, dstSize, dstSize, // dst: top-right
                                  true, false, false, gl::TextureFilter::NEAREST);
    gl::Renderer::ReadPixels(0, 0, dstSize, dstSize, px);

    const Check partialChecks[] = {
        {224, 224, 255, 0, 0, "partial blit: red now in dst top-right"},
        {64, 64, 255, 0, 0, "partial blit: dst bottom-left untouched (still red)"},
        {192, 64, 0, 255, 0, "partial blit: dst bottom-right untouched (still green)"},
    };
    for (const Check& c : partialChecks)
    {
        const gl::u8* p = &px[(c.y * dstSize + c.x) * 4];
        bool ok = (p[0] == c.r && p[1] == c.g && p[2] == c.b);
        printf("%-42s got (%3u,%3u,%3u) expected (%3u,%3u,%3u)  %s\n", c.what, p[0], p[1], p[2],
               c.r, c.g, c.b, ok ? "OK" : "FAIL");
        if (!ok) ++failed;
    }

    delete[] px;
    printf(failed == 0 ? "ALL CHECKS PASSED\n" : "%d CHECKS FAILED\n", failed);

    batch.Release();
    app.Destroy();
    return failed == 0 ? 0 : 1;
}
