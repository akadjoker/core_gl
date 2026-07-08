#pragma once

// Bunnymark: hold the mouse button to pour bunnies from the cursor.
// vsync off.
//   left held  -> +100 per frame      right held -> +1000 per frame
//   key B      -> +1000               key N      -> +100
//   key C      -> clear               ESC        -> quit
// Non-interactive (numFrames > 0): starts with 100000 bunnies and reports fps.

#include "test_common.hpp"
#include "wabbit_sprite.h" // sprite baked from assets/wabbit_alpha.png

#define BUNNY_MAX 1000000

struct Bunny
{
    float x, y, vx, vy;
    gl::u32 tint;
};

inline int test_bunnymark(int maxFrames)
{
    TestApp app;
    if (!app.Create("coregl - bunnymark")) return 1;
    SDL_GL_SetSwapInterval(0);

    gl::Batch batch;
    if (!batch.Init(65535))
    {
        fprintf(stderr, "batch init failed\n");
        app.Destroy();
        return 1;
    }

    gl::Texture bunnyTex;
    bunnyTex.Load2D(kWabbitPixels, 32, 32, gl::TextureFormat::RGBA8);
    bunnyTex.SetFilter(gl::TextureFilter::NEAREST, gl::TextureFilter::NEAREST);

    Bunny* bunnies = new Bunny[BUNNY_MAX];
    int count = 0;
    gl::u32 rng = 42;

    const float kW = 32.f, kH = 32.f; // sprite size on screen

    int winW, winH;
    SDL_GL_GetDrawableSize(app.window, &winW, &winH);

    auto spawn = [&](int n, float px, float py) {
        while (n-- > 0 && count < BUNNY_MAX)
        {
            rng = rng * 1664525u + 1013904223u;
            Bunny& b = bunnies[count++];
            b.x = px;
            b.y = py;
            b.vx = (float)(rng & 511) / 64.f - 4.f;
            b.vy = (float)(rng >> 9 & 255) / 64.f - 2.f;
            b.tint = 0xFF000000u | (rng >> 8 & 0x00FFFFFFu) | 0x00808080u;
        }
    };

    const bool autoMode = maxFrames > 0;
    if (autoMode) spawn(100000, (float)winW * 0.5f, 10.f);

    gl::Renderer::SetDepthTest(false);
    gl::Renderer::SetBlend(true);
    gl::Renderer::SetBlendFactors(gl::BlendFactor::SRC_ALPHA, gl::BlendFactor::ONE_MINUS_SRC_ALPHA);
    gl::Renderer::ClearColor(0.55f, 0.75f, 0.85f, 1.f);

    const gl::u64 freq = SDL_GetPerformanceFrequency();
    double totalMs = 0.0, smoothMs = 16.0;
    int frame = 0;
    bool running = true;

    while (running)
    {
        SDL_Event ev;
        while (SDL_PollEvent(&ev))
        {
            if (ev.type == SDL_QUIT) running = false;
            if (ev.type == SDL_KEYDOWN)
            {
                int mx, my;
                SDL_GetMouseState(&mx, &my);
                switch (ev.key.keysym.sym)
                {
                    case SDLK_ESCAPE: running = false; break;
                    case SDLK_b: spawn(1000, (float)mx, (float)my); break;
                    case SDLK_n: spawn(100, (float)mx, (float)my); break;
                    case SDLK_c: count = 0; break;
                }
            }
        }
        if (!running) break;

        // pour bunnies from the cursor while the button is held
        {
            int mx, my;
            gl::u32 buttons = SDL_GetMouseState(&mx, &my);
            if (buttons & SDL_BUTTON(SDL_BUTTON_LEFT)) spawn(100, (float)mx, (float)my);
            if (buttons & SDL_BUTTON(SDL_BUTTON_RIGHT)) spawn(1000, (float)mx, (float)my);
        }

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
        gl::Renderer::ResetStats();

        const float maxX = (float)winW - kW, maxY = (float)winH - kH;
        for (int i = 0; i < count; ++i)
        {
            Bunny& b = bunnies[i];
            b.x += b.vx;
            b.y += b.vy;
            b.vy += 0.25f; // gravity
            if (b.x < 0.f || b.x > maxX)
            {
                b.vx = -b.vx;
                b.x = b.x < 0.f ? 0.f : maxX;
            }
            if (b.y > maxY)
            {
                b.y = maxY;
                b.vy *= -0.85f;
            }
            else if (b.y < 0.f)
            {
                b.y = 0.f;
                b.vy = 0.f;
            }

            batch.SetColor((gl::u8)(b.tint & 255), (gl::u8)(b.tint >> 8 & 255),
                           (gl::u8)(b.tint >> 16 & 255), 255);
            batch.Quad(bunnyTex, b.x, b.y, kW, kH);
        }

        // HUD
        char hud[128];
        snprintf(hud, sizeof(hud), "bunnies: %d\n%.1f fps (%.2f ms)  draws: %llu", count,
                 1000.0 / smoothMs, smoothMs, (unsigned long long)gl::Renderer::GetStats().drawCalls);
        batch.SetColor(0, 0, 0, 255);
        batch.Text(11.f, 11.f, 16.f, hud);
        batch.SetColor(255, 255, 255, 255);
        batch.Text(10.f, 10.f, 16.f, hud);

        batch.Render();
        app.EndFrame();

        gl::u64 t1 = SDL_GetPerformanceCounter();
        double ms = (double)(t1 - t0) * 1000.0 / (double)freq;
        smoothMs = smoothMs * 0.95 + ms * 0.05;
        totalMs += ms;
        ++frame;
        if (autoMode && frame >= maxFrames) break;
    }

    printf("bunnies: %d | frames: %d | avg %.2f ms (%.1f fps) | draw calls/frame: %llu\n", count,
           frame, totalMs / frame, 1000.0 * frame / totalMs,
           (unsigned long long)gl::Renderer::GetStats().drawCalls);

    delete[] bunnies;
    batch.Release();
    app.Destroy();
    return 0;
}
