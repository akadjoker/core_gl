#pragma once

// Perf printout shared by the demos: every N frames, prints average draw
// calls, triangles and frame time per frame, read from the core's
// RenderStats (reset every frame so the numbers are per-frame, not
// cumulative) plus the SceneRenderer's item count. Read-only
// instrumentation — no rendering behavior changes.

#include <coregl/gl_renderer.hpp>
#include <cstdio>

struct PerfPrinter
{
    int interval = 60; // print every N frames

    gl::u64 accumNs = 0;
    gl::u64 accumDraws = 0, accumTris = 0, accumShaderSwaps = 0, accumTexBinds = 0;
    int accumItems = 0;
    int accumFrames = 0;

    // call once per frame, right after renderer.render(...); resets the
    // core's stats so the next frame starts clean
    void tick(int frame, int items, float dtSeconds)
    {
        const gl::RenderStats& s = gl::Renderer::GetStats();
        accumNs += (gl::u64)(dtSeconds * 1e9);
        accumDraws += s.drawCalls;
        accumTris += s.triangles;
        accumShaderSwaps += s.shaderSwitches;
        accumTexBinds += s.textureBinds;
        accumItems += items;
        ++accumFrames;
        gl::Renderer::ResetStats();

        if (frame == 0 || frame % interval != 0) return;

        double avgMs = (double)accumNs / (double)accumFrames / 1e6;
        printf("PERF frame %6d | avg items %4d | draws %4llu | tris %8llu | shaderSwaps %4llu | "
               "texBinds %5llu | %.2f ms/frame\n",
               frame, accumItems / accumFrames, (unsigned long long)(accumDraws / accumFrames),
               (unsigned long long)(accumTris / accumFrames),
               (unsigned long long)(accumShaderSwaps / accumFrames),
               (unsigned long long)(accumTexBinds / accumFrames), avgMs);

        accumNs = 0;
        accumDraws = accumTris = accumShaderSwaps = accumTexBinds = 0;
        accumItems = 0;
        accumFrames = 0;
    }
};
