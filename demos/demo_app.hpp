#pragma once

// Shared boilerplate for demos: SDL2 window + GL 4.3 core context + coregl
// init. Each demo creates a DemoApp, runs its frame loop and cleans up.
// F10 records a GIF of the window; COREGL_RECORD=1 starts recording at once.

#include <coregl/gl_core.hpp>
#include <scene/Filesystem.hpp>
#include <scene/Input.hpp>
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

        // demos always report GL errors through gl::Log, even in release
        gl::Renderer::EnableDebugOutput();

        // asset lookup works from the repo root, the build dir or build/demos
        fs::getFilesystem().addFolder(".");
        fs::getFilesystem().addFolder("..");
        fs::getFilesystem().addFolder("../..");

        if (getenv("COREGL_RECORD"))
        {
            int dw, dh;
            SDL_GL_GetDrawableSize(window, &dw, &dh);
            gif.Start(dw, dh);
        }

        Input::Init();
        return true;
    }

    void DrawableSize(int* w, int* h) { SDL_GL_GetDrawableSize(window, w, h); }

    // Opt-in event pump for demos that use Behavior nodes (FreeFlyBehavior,
    // OrbitBehavior, CharacterBehavior, ...): rolls Input's current->previous
    // state (Input::Update(), same ordering as tmp/core's Device::Run()),
    // then drains the SDL queue into Input::On*(). Demos that poll SDL
    // directly (see demo_fly.hpp's FlyCam) don't need this and can ignore
    // it entirely. Returns false once SDL_QUIT is seen.
    bool PollEvents()
    {
        Input::Update();
        SDL_Event event;
        while (SDL_PollEvent(&event) != 0)
        {
            switch (event.type)
            {
            case SDL_QUIT: return false;
            case SDL_KEYDOWN: Input::OnKeyDown(event.key); break;
            case SDL_KEYUP: Input::OnKeyUp(event.key); break;
            case SDL_TEXTINPUT: Input::OnTextInput(event.text); break;
            case SDL_MOUSEBUTTONDOWN: Input::OnMouseDown(event.button); break;
            case SDL_MOUSEBUTTONUP: Input::OnMouseUp(event.button); break;
            case SDL_MOUSEMOTION: Input::OnMouseMove(event.motion); break;
            case SDL_MOUSEWHEEL: Input::OnMouseWheel(event.wheel); break;
            default: break;
            }
        }
        return true;
    }

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
