#pragma once

// TUTORIAL 3 — Rotating Cube
//
// Builds on tutorial 2: real 3D. New ideas:
//   - depth testing, so nearer faces correctly hide farther ones
//   - a 24-vertex cube (4 per face, not 8) so each face can have its own
//     flat color — sharing only 8 corners would blend colors across faces
//   - a model-view-projection matrix uploaded as a uniform (SetMat4)
//
// coregl has no math types on purpose — the 4x4 matrices below are 8 lines
// of plain arrays, written by hand, column-major (OpenGL's convention).
// Bring whatever math library you like in a real project.

#include "test_common.hpp"
#include "tutorial_math.hpp"

static const char* kTutorialCubeVS = R"(#version 430 core
layout(location = 0) in vec3 a_position;
layout(location = 1) in vec3 a_color;
uniform mat4 u_mvp;
out vec3 v_color;
void main()
{
    gl_Position = u_mvp * vec4(a_position, 1.0);
    v_color = a_color;
}
)";

static const char* kTutorialCubeFS = R"(#version 430 core
in vec3 v_color;
out vec4 OutColor;
void main()
{
    OutColor = vec4(v_color, 1.0);
}
)";

inline int tutorial_03_cube(int maxFrames)
{
    TestApp app;
    if (!app.Create("tutorial 03 - cube")) return 1;

    gl::Shader shader;
    if (!shader.LoadFromString(gl::PipelineStage::VERTEX, kTutorialCubeVS) ||
        !shader.LoadFromString(gl::PipelineStage::FRAGMENT, kTutorialCubeFS) || !shader.Link())
    {
        fprintf(stderr, "shader error: %s\n", shader.GetLog());
        app.Destroy();
        return 1;
    }

    // 4 vertices per face (24 total) so each face keeps one flat color —
    // position (x,y,z) + color (r,g,b) per vertex.
    // Winding matters once culling is on: every face lists its 4 corners
    // counter-clockwise AS SEEN FROM OUTSIDE THE CUBE (OpenGL's front-face
    // convention). Get this wrong on even one face and SetCull(BACK) will
    // silently discard it — a "hole" in the cube from some angles, not a
    // crash, so it's easy to miss without checking the winding by hand.
    // clang-format off
    const float vertices[] = {
        // -Z face (red)
        -0.5f,-0.5f,-0.5f, 1,0,0,  -0.5f, 0.5f,-0.5f, 1,0,0,
         0.5f, 0.5f,-0.5f, 1,0,0,   0.5f,-0.5f,-0.5f, 1,0,0,
        // +Z face (green)
        -0.5f,-0.5f, 0.5f, 0,1,0,   0.5f,-0.5f, 0.5f, 0,1,0,
         0.5f, 0.5f, 0.5f, 0,1,0,  -0.5f, 0.5f, 0.5f, 0,1,0,
        // -X face (blue)
        -0.5f,-0.5f,-0.5f, 0,0,1,  -0.5f,-0.5f, 0.5f, 0,0,1,
        -0.5f, 0.5f, 0.5f, 0,0,1,  -0.5f, 0.5f,-0.5f, 0,0,1,
        // +X face (yellow)
         0.5f,-0.5f,-0.5f, 1,1,0,   0.5f, 0.5f,-0.5f, 1,1,0,
         0.5f, 0.5f, 0.5f, 1,1,0,   0.5f,-0.5f, 0.5f, 1,1,0,
        // -Y face (magenta)
        -0.5f,-0.5f,-0.5f, 1,0,1,   0.5f,-0.5f,-0.5f, 1,0,1,
         0.5f,-0.5f, 0.5f, 1,0,1,  -0.5f,-0.5f, 0.5f, 1,0,1,
        // +Y face (cyan)
        -0.5f, 0.5f,-0.5f, 0,1,1,  -0.5f, 0.5f, 0.5f, 0,1,1,
         0.5f, 0.5f, 0.5f, 0,1,1,   0.5f, 0.5f,-0.5f, 0,1,1,
    };
    // clang-format on

    gl::u16 indices[36];
    for (int face = 0; face < 6; ++face)
    {
        gl::u16 base = (gl::u16)(face * 4);
        gl::u16* idx = &indices[face * 6];
        idx[0] = base;
        idx[1] = base + 1;
        idx[2] = base + 2;
        idx[3] = base;
        idx[4] = base + 2;
        idx[5] = base + 3;
    }

    gl::Buffer vbo, ibo;
    vbo.Allocate(gl::BufferType::ARRAY, vertices, sizeof(vertices), gl::UsageType::STATIC_DRAW);
    ibo.Allocate(gl::BufferType::ELEMENT_ARRAY, indices, sizeof(indices),
                 gl::UsageType::STATIC_DRAW);

    const gl::VertexAttrib layout[] = {
        {gl::VertexAttribType::FLOAT, 3, 0, false}, // a_position
        {gl::VertexAttribType::FLOAT, 3, 0, false}, // a_color
    };
    gl::VertexArray vao;
    vao.AddVertexBuffer(vbo, layout, 2, 6 * sizeof(float));
    vao.SetIndexBuffer(ibo, gl::VertexAttribType::USHORT);

    const gl::i32 mvpLoc = shader.GetLocation("u_mvp");

    gl::Renderer::ClearColor(0.07f, 0.08f, 0.11f, 1.0f);
    gl::Renderer::SetDepthTest(true); // without this, faces draw in submission order, not by depth
    gl::Renderer::SetCull(gl::CullMode::BACK);

    int frame = 0;
    while (app.PollEvents())
    {
        app.BeginFrame();
        int w, h;
        SDL_GL_GetDrawableSize(app.window, &w, &h);
        gl::Renderer::Clear(true, true);

        float t = (float)frame / 60.0f;
        float proj[16], view[16], model[16], mv[16], mvp[16];
        tut_mat4_perspective(proj, 55.f, (float)w / (float)h, 0.1f, 100.f);
        tut_mat4_translate(view, 0.f, 0.f, -3.f); // move the world 3 units away from the camera
        tut_mat4_rotate_xy(model, t * 25.f, t * 40.f);
        tut_mat4_mul(mv, view, model);
        tut_mat4_mul(mvp, proj, mv);

        shader.Bind();
        shader.SetMat4(mvpLoc, mvp);
        vao.Bind();
        gl::Renderer::DrawIndexed(gl::RenderPrimitive::TRIANGLES, 36);

        app.EndFrame();
        ++frame;
        if (maxFrames > 0 && frame >= maxFrames) break;
    }

    shader.Release();
    vbo.Release();
    ibo.Release();
    vao.Release();
    app.Destroy();
    return 0;
}
