#pragma once

// Verification test: draws known shapes with the batch into a 256x256
// offscreen framebuffer and asserts the actual pixel colors with ReadPixels.
// Proves the batch geometry, indexing, transform stack and text really work —
// no eyeballing involved. Exits 0 only if every check passes.

#include "test_common.hpp"

struct PixelCheck
{
    int x, y;       // framebuffer coords (y up)
    gl::u8 r, g, b; // expected color
    const char* what;
};

inline int test_batch_verify(int /*maxFrames*/)
{
    TestApp app;
    if (!app.Create("coregl - batch verify", 320, 240)) return 1;

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

    gl::Batch batch;
    if (!batch.Init(4096))
    {
        fprintf(stderr, "FAIL: batch init\n");
        app.Destroy();
        return 1;
    }

    // ortho with (0,0) at the BOTTOM-left so batch coords == ReadPixels coords
    float proj[16] = {0};
    proj[0] = 2.f / (float)S;
    proj[5] = 2.f / (float)S;
    proj[10] = 1.f;
    proj[12] = -1.f;
    proj[13] = -1.f;
    proj[15] = 1.f;
    batch.SetProjection(proj);

    fbo.Bind();
    gl::Renderer::Viewport(0, 0, S, S);
    gl::Renderer::SetDepthTest(false);
    gl::Renderer::SetBlend(false);
    gl::Renderer::ClearColor(0.f, 0.f, 0.f, 1.f);
    gl::Renderer::Clear(true, false);

    // 1) red filled rect [16..80)x[16..80)
    batch.SetColor(255, 0, 0, 255);
    batch.Rect(16.f, 16.f, 64.f, 64.f, true);

    // 2) green filled circle center (160,48) r=30
    batch.SetColor(0, 255, 0, 255);
    batch.Circle(160.f, 48.f, 30.f, true, 48);

    // 3) blue thick horizontal line y=128, x in [16..240], thickness 10
    batch.SetColor(0, 0, 255, 255);
    batch.ThickLine(16.f, 128.f, 240.f, 128.f, 10.f);

    // 4) white rect drawn at origin, moved to (192,192) by the transform stack
    batch.PushMatrix();
    batch.Translate(192.f, 192.f);
    batch.Rotate(90.f, 0.f, 0.f, 1.f); // square: rotation must not change coverage
    batch.SetColor(255, 255, 255, 255);
    batch.Rect(-16.f, -16.f, 32.f, 32.f, true);
    batch.PopMatrix();

    // 5) yellow text block: '#' of the 8x8 font at size 32 -> dense glyph at (32,192)
    batch.SetColor(255, 255, 0, 255);
    batch.Text(32.f, 176.f, 32.f, "#");

    batch.Render();

    const gl::RenderStats& st = gl::Renderer::GetStats();
    printf("DEBUG: draw calls %llu | vao %llu | shader %llu | buffers %llu | verts %u idx %u\n",
           (unsigned long long)st.drawCalls, (unsigned long long)st.vaoSwitches,
           (unsigned long long)st.shaderSwitches, (unsigned long long)st.bufferBinds,
           batch.GetVertexCount(), batch.GetIndexCount());

    gl::u8* px = new gl::u8[S * S * 4];
    gl::Renderer::ReadPixels(0, 0, S, S, px);

    const PixelCheck checks[] = {
        {48, 48, 255, 0, 0, "red rect center"},
        {17, 17, 255, 0, 0, "red rect corner"},
        {90, 48, 0, 0, 0, "outside red rect = background"},
        {160, 48, 0, 255, 0, "green circle center"},
        {160, 68, 0, 255, 0, "green circle inner edge"},
        {160, 90, 0, 0, 0, "outside green circle"},
        {128, 128, 0, 0, 255, "thick line center"},
        {128, 140, 0, 0, 0, "above thick line"},
        {192, 192, 255, 255, 255, "transformed rect center"},
        {192, 206, 255, 255, 255, "transformed rect top (rotated square)"},
        {192, 216, 0, 0, 0, "outside transformed rect"},
    };

    int failed = 0;
    for (const PixelCheck& c : checks)
    {
        // note: text row checked separately below (glyph coverage, not exact px)
        const gl::u8* p = &px[(c.y * S + c.x) * 4];
        bool ok = (p[0] == c.r && p[1] == c.g && p[2] == c.b);
        printf("%-42s (%3d,%3d) got (%3u,%3u,%3u) expected (%3u,%3u,%3u)  %s\n", c.what, c.x, c.y,
               p[0], p[1], p[2], c.r, c.g, c.b, ok ? "OK" : "FAIL");
        if (!ok) ++failed;
    }

    // text check: the '#' glyph must have painted a decent number of yellow
    // pixels inside its 32x32 cell at (32,176)
    int yellow = 0;
    for (int y = 176; y < 208; ++y)
        for (int x = 32; x < 64; ++x)
        {
            const gl::u8* p = &px[(y * S + x) * 4];
            if (p[0] == 255 && p[1] == 255 && p[2] == 0) ++yellow;
        }
    bool textOk = yellow > 100; // '#' covers well over 100 of 1024 pixels
    printf("%-42s yellow pixels in glyph cell: %d  %s\n", "text glyph '#' coverage", yellow,
           textOk ? "OK" : "FAIL");
    if (!textOk) ++failed;

    delete[] px;

    printf(failed == 0 ? "ALL CHECKS PASSED\n" : "%d CHECKS FAILED\n", failed);

    batch.Release();
    app.Destroy();
    return failed == 0 ? 0 : 1;
}
