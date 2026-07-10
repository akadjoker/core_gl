#pragma once

// Free-fly camera controller shared by the demos: WASD/QE + hold left mouse
// to look, LSHIFT for speed. The demo owns the SDL loop; this only turns
// events and keys into camera motion.

#include <SDL2/SDL.h>
#include <scene/Camera3D.hpp>

struct FlyCam
{
    float yaw = 0.f;
    float pitch = -0.15f;
    float speed = 18.f;
    bool looking = false;

    void handle(const SDL_Event& ev)
    {
        if (ev.type == SDL_MOUSEBUTTONDOWN && ev.button.button == SDL_BUTTON_LEFT) looking = true;
        if (ev.type == SDL_MOUSEBUTTONUP && ev.button.button == SDL_BUTTON_LEFT) looking = false;
        if (ev.type == SDL_MOUSEMOTION && looking)
        {
            yaw -= (float)ev.motion.xrel * 0.005f;
            pitch -= (float)ev.motion.yrel * 0.005f;
            if (pitch > 1.55f) pitch = 1.55f;
            if (pitch < -1.55f) pitch = -1.55f;
        }
    }

    void apply(Camera3D* cam, float dt)
    {
        cam->set_euler(Vec3(pitch, yaw, 0.f));
        const Uint8* keys = SDL_GetKeyboardState(nullptr);
        float v = keys[SDL_SCANCODE_LSHIFT] ? speed * 4.f : speed;
        if (keys[SDL_SCANCODE_W]) cam->advance(v * dt);
        if (keys[SDL_SCANCODE_S]) cam->advance(-v * dt);
        if (keys[SDL_SCANCODE_A]) cam->strafe(-v * dt);
        if (keys[SDL_SCANCODE_D]) cam->strafe(v * dt);
        if (keys[SDL_SCANCODE_Q]) cam->move_global(Vec3(0.f, -v * dt, 0.f));
        if (keys[SDL_SCANCODE_E]) cam->move_global(Vec3(0.f, v * dt, 0.f));
    }
};
