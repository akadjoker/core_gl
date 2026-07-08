#pragma once

// Test: 2D batch — shapes, lines and textured quads with an ortho projection
// passed as a raw float[16] (no math library involved).

#include "test_common.hpp"
#include <cmath>

// column-major ortho: (0,0) top-left, (w,h) bottom-right
inline void makeOrtho(float* m, float w, float h)
{
    for (int i = 0; i < 16; ++i)
        m[i] = 0.f;
    m[0] = 2.f / w;
    m[5] = -2.f / h;
    m[10] = 1.f;
    m[12] = -1.f;
    m[13] = 1.f;
    m[15] = 1.f;
}

inline int test_batch(int maxFrames)
{
    TestApp app;
    if (!app.Create("coregl - batch")) return 1;

    gl::Batch batch;
    if (!batch.Init(16384))
    {
        fprintf(stderr, "batch init failed\n");
        app.Destroy();
        return 1;
    }

    // procedural checkerboard texture
    const int kTexSize = 64;
    gl::u8 pixels[kTexSize * kTexSize * 4];
    for (int y = 0; y < kTexSize; ++y)
    {
        for (int x = 0; x < kTexSize; ++x)
        {
            gl::u8 c = (((x >> 3) + (y >> 3)) & 1) ? 230 : 60;
            gl::u8* p = &pixels[(y * kTexSize + x) * 4];
            p[0] = c;
            p[1] = c;
            p[2] = c;
            p[3] = 255;
        }
    }
    gl::Texture checker;
    checker.Load2D(pixels, kTexSize, kTexSize, gl::TextureFormat::RGBA8);

    gl::Renderer::SetDepthTest(false);
    gl::Renderer::SetBlend(true);
    gl::Renderer::SetBlendFactors(gl::BlendFactor::SRC_ALPHA, gl::BlendFactor::ONE_MINUS_SRC_ALPHA);
    gl::Renderer::ClearColor(0.10f, 0.10f, 0.14f, 1.0f);

    int frame = 0;
    while (app.PollEvents())
    {
        app.BeginFrame();
        gl::Renderer::Clear(true, false);

        int w, h;
        SDL_GL_GetDrawableSize(app.window, &w, &h);
        float proj[16];
        makeOrtho(proj, (float)w, (float)h);
        batch.SetProjection(proj);

        float t = (float)frame / 60.0f;

        // grid of filled rects (same texture+mode -> should merge into 1 draw)
        for (int i = 0; i < 8; ++i)
        {
            for (int j = 0; j < 4; ++j)
            {
                batch.SetColor((gl::u8)(60 + i * 24), (gl::u8)(80 + j * 40), 200, 255);
                batch.Rect(40.f + i * 70.f, 40.f + j * 70.f, 60.f, 60.f, true);
            }
        }

        // circles: one filled pulsing, one outline
        batch.SetColor(255, 180, 60, 255);
        batch.Circle(700.f, 150.f, 60.f + 15.f * sinf(t * 2.f), true, 48);
        batch.SetColor(255, 255, 255, 255);
        batch.Circle(700.f, 150.f, 90.f, false, 48);

        // transform stack: rects spinning around their own centers
        for (int i = 0; i < 4; ++i)
        {
            batch.PushMatrix();
            batch.Translate(620.f + i * 90.f, 560.f, 0.f);
            batch.Rotate(t * 60.f + i * 45.f, 0.f, 0.f, 1.f);
            batch.SetColor((gl::u8)(120 + i * 30), 120, (gl::u8)(255 - i * 40), 255);
            batch.Rect(-25.f, -25.f, 50.f, 50.f, true);
            batch.PopMatrix();
        }

        // more 2D primitives
        batch.SetColor(255, 210, 80, 255);
        batch.Ring(180.f, 620.f, 40.f, 65.f, true, 40);
        batch.SetColor(160, 240, 200, 255);
        batch.Polygon(340.f, 620.f, 6, 55.f, t * 30.f, false);
        batch.SetColor(255, 120, 200, 255);
        batch.ThickLine(420.f, 560.f, 540.f, 680.f, 8.f);
        batch.SetColor(200, 200, 220, 160);
        batch.Arc(340.f, 620.f, 80.f, 0.f, 90.f + 260.f * (0.5f + 0.5f * sinf(t)), 32);

        // line fan
        batch.SetColor(120, 220, 160, 255);
        for (int i = 0; i < 24; ++i)
        {
            float a = t + (float)i * 0.26f;
            batch.Line(880.f, 420.f, 880.f + cosf(a) * 110.f, 420.f + sinf(a) * 110.f);
        }

        // textured quads: full + atlas sub-rect, tinted
        batch.SetColor(255, 255, 255, 255);
        batch.Quad(checker, 60.f, 340.f, 180.f, 180.f);
        batch.SetColor(255, 120, 120, 255);
        batch.Quad(checker, 0.f, 0.f, 32.f, 32.f, 300.f, 340.f, 180.f, 180.f);

        // text with the embedded font
        batch.SetColor(255, 255, 255, 255);
        batch.Text(40.f, (float)h - 90.f, 32.f, "coregl batch - embedded 8x8 font");
        batch.SetColor(140, 200, 255, 255);
        char info[128];
        snprintf(info, sizeof(info), "frame %d | verts %u | ABCDEFGHIJKLMNOPQRSTUVWXYZ 0123456789",
                 frame, batch.GetVertexCount());
        batch.Text(40.f, (float)h - 50.f, 16.f, info);

        gl::u32 vertsThisFrame = batch.GetVertexCount();
        batch.Render();

        if (frame == 0)
            printf("batch vertices/frame: %u | draw calls/frame: %llu\n", vertsThisFrame,
                   (unsigned long long)gl::Renderer::GetStats().drawCalls);

        app.EndFrame();
        ++frame;
        if (maxFrames > 0 && frame >= maxFrames) break;
    }

    const gl::RenderStats& st = gl::Renderer::GetStats();
    printf("frames: %d | draw calls: %llu (%.1f per frame) | texture binds: %llu\n", frame,
           (unsigned long long)st.drawCalls, frame ? (double)st.drawCalls / frame : 0.0,
           (unsigned long long)st.textureBinds);

    app.Destroy();
    return 0;
}
