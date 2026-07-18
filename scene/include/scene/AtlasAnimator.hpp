#pragma once

#include "scene/Math.hpp"

// Flip-book animation over an NxM texture atlas: advances through every
// cell in row-major order at a fixed frame rate, looping forever. A plain
// value type (no GL, no node) — anything that samples a sprite sheet over
// time can hold one and ask for the current frame's UV rect. Used by
// BillboardNode's set_animated_grid() today; ParticleSystemNode's
// per-particle atlas frames are a natural future adopter.
struct AtlasAnimator
{
    int cols = 1, rows = 1;
    float fps = 12.f;
    float time = 0.f;

    void set_grid(int c, int r)
    {
        cols = c > 0 ? c : 1;
        rows = r > 0 ? r : 1;
    }

    void update(float dt) { time += dt; }

    // normalized (u0, v0, width, height) for whichever frame `time` lands
    // on — row-major order (frame 0 = top-left, advances left-to-right
    // then down), looping once it runs past the last cell.
    Vec4 uv_rect() const
    {
        int total = cols * rows;
        int frame = total > 0 ? (int)(time * fps) % total : 0;
        if (frame < 0) frame += total;
        int col = frame % cols, row = frame / cols;
        float w = 1.f / (float)cols, h = 1.f / (float)rows;
        return Vec4((float)col * w, (float)row * h, w, h);
    }
};
