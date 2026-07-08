#pragma once

// Test: animated clear color — exercises the state cache.

#include "test_common.hpp"
#include <cmath>

inline int test_clear(int maxFrames)
{
    TestApp app;
    if (!app.Create("coregl - clear")) return 1;

    gl::Renderer::SetDepthTest(true);

    int frame = 0;
    while (app.PollEvents())
    {
        app.BeginFrame();

        float t = (float)frame / 60.0f;
        gl::Renderer::ClearColor(0.5f + 0.5f * sinf(t), 0.5f + 0.5f * sinf(t + 2.1f),
                                 0.5f + 0.5f * sinf(t + 4.2f), 1.0f);
        gl::Renderer::Clear(true, true);

        app.EndFrame();
        ++frame;
        if (maxFrames > 0 && frame >= maxFrames) break;
    }

    const gl::RenderStats& st = gl::Renderer::GetStats();
    printf("frames: %d | state changes: %llu (clear color changes every frame, "
           "viewport/depth only on the first)\n",
           frame, (unsigned long long)st.stateChanges);

    app.Destroy();
    return 0;
}
