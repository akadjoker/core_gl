#pragma once

// Simplified bunnymark for performance testing.
// Renders a fixed number of bunnies and measures frame rate.

#include "test_common.hpp"
#include "wabbit_sprite.h"

#define BUNNY_COUNT 100000

struct PerfBunny
{
    float x, y, vx, vy;
};

inline int test_perf(int maxFrames)
{
    TestApp app;
    if (!app.Create("coregl - perf test")) return 1;
    SDL_GL_SetSwapInterval(0); // VSync off

    gl::Batch batch;
    if (!batch.Init(BUNNY_COUNT)) // Pre-allocate for all bunnies
    {
        fprintf(stderr, "batch init failed\n");
        app.Destroy();
        return 1;
    }

    gl::Texture bunnyTex;
    bunnyTex.Load2D(kWabbitPixels, 32, 32, gl::TextureFormat::RGBA8);
    bunnyTex.SetFilter(gl::TextureFilter::NEAREST, gl::TextureFilter::NEAREST);

    PerfBunny* bunnies = new PerfBunny[BUNNY_COUNT];
    gl::u32 rng = 42;

    int winW, winH;
    SDL_GL_GetDrawableSize(app.window, &winW, &winH);

    for (int i = 0; i < BUNNY_COUNT; ++i)
    {
        rng = rng * 1664525u + 1013904223u;
        PerfBunny& b = bunnies[i];
        b.x = (float)(rng % winW);
        b.y = (float)((rng >> 16) % winH);
        b.vx = (float)(rng & 511) / 64.f - 4.f;
        b.vy = (float)(rng >> 9 & 255) / 64.f - 2.f;
    }

    gl::Renderer::SetDepthTest(false);
    gl::Renderer::SetBlend(true);
    gl::Renderer::SetBlendFactors(gl::BlendFactor::SRC_ALPHA, gl::BlendFactor::ONE_MINUS_SRC_ALPHA);
    gl::Renderer::ClearColor(0.1f, 0.2f, 0.3f, 1.f);

    const gl::u64 freq = SDL_GetPerformanceFrequency();
    double totalMs = 0.0;
    double smoothMs = 16.0;
    int frame = 0;
    bool running = true;
    const bool autoMode = maxFrames > 0;

    while (running)
    {
        SDL_Event ev;
        while (SDL_PollEvent(&ev))
        {
            if (ev.type == SDL_QUIT || (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_ESCAPE))
            {
                running = false;
            }
        }
        if (!running) break;

        gl::u64 t0 = SDL_GetPerformanceCounter();

        app.BeginFrame();
        SDL_GL_GetDrawableSize(app.window, &winW, &winH);
        
        float proj[16] = {0};
        proj[0] = 2.f / (float)winW;
        proj[5] = -2.f / (float)winH;
        proj[10] = 1.f;
        proj[12] = -1.f;
        proj[13] = 1.f;
        proj[15] = 1.f;
        batch.SetProjection(proj);

        gl::Renderer::Clear(true, false);
        
        const float kW = 32.f, kH = 32.f; // sprite size
        const float maxX = (float)winW - kW, maxY = (float)winH - kH;
        
        for (int i = 0; i < BUNNY_COUNT; ++i)
        {
            PerfBunny& b = bunnies[i];
            b.x += b.vx;
            b.y += b.vy;
            b.vy += 0.25f; // gravity

            if (b.x < 0.f || b.x > maxX) { b.vx = -b.vx; b.x = b.x < 0.f ? 0.f : maxX; }
            if (b.y > maxY) { b.y = maxY; b.vy *= -0.85f; }
            else if (b.y < 0.f) { b.y = 0.f; b.vy = 0.f; }

            batch.Quad(bunnyTex, b.x, b.y, kW, kH);
        }

        char hud[64];
        snprintf(hud, sizeof(hud), "%d bunnies\n%.1f FPS (%.2f ms)", BUNNY_COUNT,
                 1000.0 / smoothMs, smoothMs);
        batch.SetColor(0, 0, 0, 255);
        batch.Text(6.f, 6.f, 16.f, hud);
        batch.SetColor(255, 255, 255, 255);
        batch.Text(5.f, 5.f, 16.f, hud);

        batch.Render();
        app.EndFrame();

        gl::u64 t1 = SDL_GetPerformanceCounter();
        double ms = (double)(t1 - t0) * 1000.0 / (double)freq;
        smoothMs = smoothMs * 0.95 + ms * 0.05;
        totalMs += ms;
        ++frame;

        if (autoMode && frame >= maxFrames) running = false;
    }

    printf("Bunnies: %d | Frames: %d | Avg %.2f ms (%.1f fps)\n", BUNNY_COUNT, frame,
           totalMs / frame, 1000.0 * frame / totalMs);

    delete[] bunnies;
    batch.Release();
    app.Destroy();
    return 0;
}
