#pragma once

// 3D shapes through the batch: solid cube/sphere/cylinder/capsule spinning
// over a grid, wireframe versions orbiting them. Perspective matrix written
// by hand in the test — the library stays math-free.

#include "test_common.hpp"
#include <cmath>

// column-major helpers (test-local, not part of the library)
inline void mat_identity(float* m)
{
    for (int i = 0; i < 16; ++i)
        m[i] = 0.f;
    m[0] = m[5] = m[10] = m[15] = 1.f;
}

inline void mat_mul(float* out, const float* a, const float* b) // out = a * b
{
    float r[16];
    for (int c = 0; c < 4; ++c)
        for (int i = 0; i < 4; ++i)
            r[c * 4 + i] = a[i] * b[c * 4] + a[4 + i] * b[c * 4 + 1] + a[8 + i] * b[c * 4 + 2] +
                           a[12 + i] * b[c * 4 + 3];
    for (int i = 0; i < 16; ++i)
        out[i] = r[i];
}

inline void mat_perspective(float* m, float fovyDeg, float aspect, float zNear, float zFar)
{
    for (int i = 0; i < 16; ++i)
        m[i] = 0.f;
    const float f = 1.f / tanf(fovyDeg * 0.00872664626f); // deg/2 -> rad
    m[0] = f / aspect;
    m[5] = f;
    m[10] = (zFar + zNear) / (zNear - zFar);
    m[11] = -1.f;
    m[14] = 2.f * zFar * zNear / (zNear - zFar);
}

inline void mat_translate(float* m, float x, float y, float z)
{
    mat_identity(m);
    m[12] = x;
    m[13] = y;
    m[14] = z;
}

inline void mat_rotate_x(float* m, float deg)
{
    mat_identity(m);
    const float c = cosf(deg * 0.01745329252f), s = sinf(deg * 0.01745329252f);
    m[5] = c;
    m[6] = s;
    m[9] = -s;
    m[10] = c;
}

inline int test_shapes3d(int maxFrames)
{
    TestApp app;
    if (!app.Create("coregl - 3D shapes")) return 1;

    gl::Batch batch;
    if (!batch.Init(65535))
    {
        fprintf(stderr, "batch init failed\n");
        app.Destroy();
        return 1;
    }

    gl::Renderer::ClearColor(0.07f, 0.08f, 0.11f, 1.f);
    // all Batch solids are wound CCW as seen from outside — see
    // test_winding_verify.hpp — so normal backface culling is safe here
    gl::Renderer::SetCull(gl::CullMode::BACK);

    int frame = 0;
    while (app.PollEvents())
    {
        app.BeginFrame();
        int w, h;
        SDL_GL_GetDrawableSize(app.window, &w, &h);
        float t = (float)frame / 60.f;

        gl::Renderer::SetDepthTest(true);
        gl::Renderer::SetBlend(false);
        gl::Renderer::Clear(true, true);
        gl::Renderer::ResetStats();

        // camera: perspective * tilt down * pull back
        float proj[16], view[16], tilt[16], pv[16];
        mat_perspective(proj, 55.f, (float)w / (float)h, 0.1f, 100.f);
        mat_translate(view, 0.f, -1.2f, -9.f);
        mat_rotate_x(tilt, 18.f);
        mat_mul(view, view, tilt);
        mat_mul(pv, proj, view);
        batch.SetProjection(pv);

        // floor grid + world axes
        batch.SetColor(70, 75, 95, 255);
        batch.Grid3D(12.f, 1.f);
        batch.Axes(2.5f);

        struct Placement
        {
            float x;
            int kind; // 0 cube, 1 sphere, 2 cylinder, 3 capsule
        };
        const Placement items[4] = {{-4.5f, 0}, {-1.5f, 1}, {1.5f, 2}, {4.5f, 3}};
        const gl::u8 colors[4][3] = {{235, 90, 80}, {80, 200, 120}, {90, 140, 240}, {230, 200, 80}};

        for (int i = 0; i < 4; ++i)
        {
            batch.PushMatrix();
            batch.Translate(items[i].x, 1.4f + 0.3f * sinf(t * 2.f + (float)i), 0.f);
            batch.Rotate(t * 40.f + (float)i * 90.f, 0.3f, 1.f, 0.1f);

            batch.SetColor(colors[i][0], colors[i][1], colors[i][2], 255);
            switch (items[i].kind)
            {
                case 0:
                    batch.Cube(0, 0, 0, 1.6f, 1.6f, 1.6f);
                    break;
                case 1:
                    batch.Sphere(0, 0, 0, 1.0f, 14, 24);
                    break;
                case 2:
                    batch.Cylinder(0, 0, 0, 0.8f, 1.8f, 24);
                    break;
                case 3:
                    batch.Capsule(0, 0, 0, 0.6f, 2.4f, 8, 20);
                    break;
            }

            // white wireframe overlaid on the same transform
            batch.SetColor(255, 255, 255, 255);
            switch (items[i].kind)
            {
                case 0:
                    batch.CubeWire(0, 0, 0, 1.62f, 1.62f, 1.62f);
                    break;
                case 1:
                    batch.SphereWire(0, 0, 0, 1.01f, 8, 16);
                    break;
                case 2:
                    batch.CylinderWire(0, 0, 0, 0.81f, 1.82f, 16);
                    break;
                case 3:
                    batch.CapsuleWire(0, 0, 0, 0.61f, 2.42f, 16);
                    break;
            }
            batch.PopMatrix();
        }
        batch.Render();

        // HUD in a second pass with an ortho projection, depth off
        gl::Renderer::SetDepthTest(false);
        float ortho[16] = {0};
        ortho[0] = 2.f / (float)w;
        ortho[5] = -2.f / (float)h;
        ortho[10] = 1.f;
        ortho[12] = -1.f;
        ortho[13] = 1.f;
        ortho[15] = 1.f;
        batch.SetProjection(ortho);

        const gl::RenderStats& st = gl::Renderer::GetStats();
        char hud[128];
        snprintf(hud, sizeof(hud),
                 "cube sphere cylinder capsule\ntris: %llu  lines: %llu  draws: %llu",
                 (unsigned long long)st.triangles, (unsigned long long)st.lines,
                 (unsigned long long)st.drawCalls);
        batch.SetColor(255, 255, 255, 255);
        batch.Text(10.f, 10.f, 16.f, hud);
        batch.Render();

        app.EndFrame();
        ++frame;
        if (maxFrames > 0 && frame >= maxFrames) break;
    }

    const gl::RenderStats& st = gl::Renderer::GetStats();
    printf("frames: %d | last frame: %llu triangles, %llu lines, %llu draw calls\n", frame,
           (unsigned long long)st.triangles, (unsigned long long)st.lines,
           (unsigned long long)st.drawCalls);

    batch.Release();
    app.Destroy();
    return 0;
}
