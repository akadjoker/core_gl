#pragma once

// Shared boilerplate for demos: SDL2 window + GL 4.3 core context + coregl
// init. Each demo creates a DemoApp, runs its frame loop and cleans up.
// F10 records a GIF of the window; COREGL_RECORD=1 starts recording at once.

#include <coregl/gl_core.hpp>
#include <SDL2/SDL.h>
#include <cstdio>
#include <cstdlib>
#include "gif_recorder.hpp"

struct DemoApp
{
    SDL_Window* window = nullptr;
    SDL_GLContext context = nullptr;
    GifRecorder gif;

    bool Create(const char* title, int w = 1280, int h = 720)
    {
        if (SDL_Init(SDL_INIT_VIDEO) != 0)
        {
            fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
            return false;
        }

        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
        SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
        SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);

        window = SDL_CreateWindow(title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, w, h,
                                  SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
        if (!window)
        {
            fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError());
            SDL_Quit();
            return false;
        }

        context = SDL_GL_CreateContext(window);
        if (!context)
        {
            fprintf(stderr, "SDL_GL_CreateContext: %s\n", SDL_GetError());
            SDL_DestroyWindow(window);
            SDL_Quit();
            return false;
        }
        SDL_GL_SetSwapInterval(1);

        if (!gl::Renderer::Init(SDL_GL_GetProcAddress))
        {
            fprintf(stderr, "gl::Renderer::Init failed\n");
            Destroy();
            return false;
        }

        printf("GL version : %s\n", gl::Renderer::GetVersionString());
        printf("GL renderer: %s\n", gl::Renderer::GetRendererString());

        if (getenv("COREGL_RECORD"))
        {
            int dw, dh;
            SDL_GL_GetDrawableSize(window, &dw, &dh);
            gif.Start(dw, dh);
        }
        return true;
    }

    void DrawableSize(int* w, int* h) { SDL_GL_GetDrawableSize(window, w, h); }

    // Captures the just-rendered backbuffer for the GIF recorder (if active)
    // before it gets swapped, then presents the frame.
    void EndFrame()
    {
        gif.Capture();
        SDL_GL_SwapWindow(window);
    }

    void Destroy()
    {
        gif.Stop(); // flush a pending recording so it's never lost
        gl::Renderer::Shutdown();
        if (context) SDL_GL_DeleteContext(context);
        if (window) SDL_DestroyWindow(window);
        SDL_Quit();
        window = nullptr;
        context = nullptr;
    }
};
