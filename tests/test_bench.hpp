#pragma once

// Benchmark: N textured quads per frame through the batch, vsync off.
// Measures CPU submit time (build + upload + draw calls) and whole-frame time.

#include "test_common.hpp"
#include <cmath>

inline int test_bench(int maxFrames)
{
    const int kQuads = 50000;
    if (maxFrames <= 0) maxFrames = 300;

    TestApp app;
    if (!app.Create("coregl - bench")) return 1;
    SDL_GL_SetSwapInterval(0); // vsync off: measure real cost

    gl::Batch batch;
    if (!batch.Init(65535))
    {
        fprintf(stderr, "batch init failed\n");
        app.Destroy();
        return 1;
    }

    // small checkerboard so the texture unit does real work
    const int T = 32;
    gl::u8 pixels[T * T * 4];
    for (int i = 0; i < T * T; ++i)
    {
        gl::u8 c = ((i ^ (i / T)) & 4) ? 220 : 90;
        pixels[i * 4] = c;
        pixels[i * 4 + 1] = c;
        pixels[i * 4 + 2] = c;
        pixels[i * 4 + 3] = 255;
    }
    gl::Texture tex;
    tex.Load2D(pixels, T, T, gl::TextureFormat::RGBA8);

    gl::Renderer::SetDepthTest(false);
    gl::Renderer::SetBlend(false);
    gl::Renderer::ClearColor(0.05f, 0.05f, 0.08f, 1.f);

    const gl::u64 freq = SDL_GetPerformanceFrequency();
    double submitMs = 0.0, frameMs = 0.0;
    int frame = 0;

    while (app.PollEvents() && frame < maxFrames)
    {
        gl::u64 t0 = SDL_GetPerformanceCounter();

        app.BeginFrame();
        int w, h;
        SDL_GL_GetDrawableSize(app.window, &w, &h);
        float proj[16] = {0};
        proj[0] = 2.f / (float)w;
        proj[5] = -2.f / (float)h;
        proj[10] = 1.f;
        proj[12] = -1.f;
        proj[13] = 1.f;
        proj[15] = 1.f;
        batch.SetProjection(proj);

        gl::Renderer::Clear(true, false);
        gl::Renderer::ResetStats();

        // 50k quads, deterministic pseudo-random placement
        gl::u32 rng = 12345 + (gl::u32)frame;
        for (int i = 0; i < kQuads; ++i)
        {
            rng = rng * 1664525u + 1013904223u;
            float x = (float)(rng >> 16 & 1023);
            float y = (float)(rng >> 6 & 511);
            batch.SetColor((gl::u8)(rng & 255), (gl::u8)(rng >> 8 & 255), 200, 255);
            batch.Quad(tex, x, y, 12.f, 12.f);
        }
        batch.Render();

        gl::u64 t1 = SDL_GetPerformanceCounter(); // CPU submit done
        app.EndFrame();                           // swap (waits for GPU queue)
        gl::u64 t2 = SDL_GetPerformanceCounter();

        submitMs += (double)(t1 - t0) * 1000.0 / (double)freq;
        frameMs += (double)(t2 - t0) * 1000.0 / (double)freq;
        ++frame;
    }

    const gl::RenderStats& st = gl::Renderer::GetStats();
    double avgSubmit = submitMs / frame, avgFrame = frameMs / frame;
    printf("quads/frame: %d (%d verts, %d indices)\n", kQuads, kQuads * 4, kQuads * 6);
    printf("draw calls/frame: %llu | buffer binds/frame: %llu\n",
           (unsigned long long)st.drawCalls, (unsigned long long)st.bufferBinds);
    printf("CPU submit: %.2f ms/frame | full frame: %.2f ms (%.0f fps)\n", avgSubmit, avgFrame,
           1000.0 / avgFrame);
    printf("throughput: %.1f M quads/s (CPU submit)\n",
           (double)kQuads / (avgSubmit / 1000.0) / 1e6);

    batch.Release();
    app.Destroy();
    return 0;
}
