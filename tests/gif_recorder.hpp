#pragma once

// F10 toggles GIF recording of whatever the app is rendering. Wired into
// TestApp (test_common.hpp) — every test/tutorial gets it for free, nothing
// to add at the call site. Captures the backbuffer via Renderer::ReadPixels
// right before it is swapped, throttled to ~15 fps regardless of the app's
// real frame rate, and writes coregl_capture_<timestamp>.gif on stop.

#define MSF_GIF_IMPL
#include "msf_gif.h"

#include <coregl/gl_core.hpp>
#include <SDL2/SDL.h>
#include <cstdio>
#include <ctime>

struct GifRecorder
{
    MsfGifState state;
    bool recording = false;
    int width = 0, height = 0;
    gl::u8* pixels = nullptr;
    gl::u64 lastCaptureTicks = 0;
    int frameCount = 0;

    static const int kCentiSecondsPerFrame = 6; // ~16.6 fps in the output gif

    void Toggle(int w, int h)
    {
        if (recording)
            Stop();
        else
            Start(w, h);
    }

    void Start(int w, int h)
    {
        width = w;
        height = h;
        pixels = new gl::u8[(size_t)w * (size_t)h * 4];

        MsfGifState fresh = {};
        state = fresh;
        if (!msf_gif_begin(&state, w, h))
        {
            fprintf(stderr, "GifRecorder: msf_gif_begin failed\n");
            delete[] pixels;
            pixels = nullptr;
            return;
        }

        lastCaptureTicks = 0;
        frameCount = 0;
        recording = true;
        printf("GifRecorder: recording started (%dx%d) — press F10 again to stop\n", w, h);
    }

    // Call once per frame, before the backbuffer is swapped.
    void Capture()
    {
        if (!recording || !pixels) return;

        gl::u64 now = SDL_GetPerformanceCounter();
        gl::u64 freq = SDL_GetPerformanceFrequency();
        double ms = lastCaptureTicks == 0
                        ? 1e9
                        : (double)(now - lastCaptureTicks) * 1000.0 / (double)freq;
        if (ms < (double)kCentiSecondsPerFrame * 10.0) return; // throttle to ~15 fps
        lastCaptureTicks = now;

        gl::Renderer::ReadPixels(0, 0, width, height, pixels);
        // negative pitch flips rows: glReadPixels returns bottom-up, gif wants top-down
        msf_gif_frame(&state, pixels, kCentiSecondsPerFrame, 16, -(width * 4));
        ++frameCount;
    }

    void Stop()
    {
        if (!recording) return;
        recording = false;

        MsfGifResult result = msf_gif_end(&state);
        if (result.data)
        {
            char filename[256];
            std::time_t t = std::time(nullptr);
            snprintf(filename, sizeof(filename), "coregl_capture_%ld.gif", (long)t);
            FILE* fp = fopen(filename, "wb");
            if (fp)
            {
                fwrite(result.data, result.dataSize, 1, fp);
                fclose(fp);
                printf("GifRecorder: saved %s (%d frames, %.1f KB)\n", filename, frameCount,
                       (double)result.dataSize / 1024.0);
            }
            else
            {
                fprintf(stderr, "GifRecorder: could not open %s for writing\n", filename);
            }
        }
        msf_gif_free(result);

        delete[] pixels;
        pixels = nullptr;
    }

    ~GifRecorder()
    {
        if (recording) Stop();
    }
};
