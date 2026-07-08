#pragma once

// TUTORIAL 1 — Hello Triangle
//
// The five objects every coregl program touches, in the order you create
// them:
//   1. Shader       — compile GLSL, link a program
//   2. Buffer        — upload vertex data to the GPU (a VBO)
//   3. VertexArray   — describe how to read that data (the layout)
//   4. Renderer      — clear the screen, issue the draw call
//
// No textures, no matrices, no uniforms that change per frame — just enough
// to get a colored triangle on screen and see where each piece plugs in.

#include "test_common.hpp"

// STEP 1: GLSL source as plain strings. coregl compiles from strings only —
// no file loading, no #include resolution inside the library.
static const char* kTutorialTriangleVS = R"(#version 430 core
layout(location = 0) in vec2 a_position;
layout(location = 1) in vec3 a_color;
out vec3 v_color;
void main()
{
    gl_Position = vec4(a_position, 0.0, 1.0);
    v_color = a_color;
}
)";

static const char* kTutorialTriangleFS = R"(#version 430 core
in vec3 v_color;
out vec4 OutColor;
void main()
{
    OutColor = vec4(v_color, 1.0);
}
)";

inline int tutorial_01_triangle(int maxFrames)
{
    TestApp app;
    if (!app.Create("tutorial 01 - triangle")) return 1;

    // STEP 2: compile + link. Each stage is loaded from a string, then
    // Link() combines them into one GL program. Uniform locations (none
    // here) get resolved once, inside Link() — see GetLog() on failure.
    gl::Shader shader;
    if (!shader.LoadFromString(gl::PipelineStage::VERTEX, kTutorialTriangleVS) ||
        !shader.LoadFromString(gl::PipelineStage::FRAGMENT, kTutorialTriangleFS) || !shader.Link())
    {
        fprintf(stderr, "shader error: %s\n", shader.GetLog());
        app.Destroy();
        return 1;
    }

    // STEP 3: vertex data. Position (x,y) and color (r,g,b) are interleaved
    // per vertex: 5 floats per vertex, 3 vertices, no indices needed for a
    // single triangle.
    const float vertices[] = {
        // x,     y,     r,    g,    b
        0.0f,  0.6f,  1.0f, 0.2f, 0.2f, // top, red
        -0.6f, -0.6f, 0.2f, 1.0f, 0.2f, // bottom-left, green
        0.6f,  -0.6f, 0.2f, 0.2f, 1.0f, // bottom-right, blue
    };
    gl::Buffer vbo;
    vbo.Allocate(gl::BufferType::ARRAY, vertices, sizeof(vertices), gl::UsageType::STATIC_DRAW);

    // STEP 4: describe the layout above to the VAO — 2 floats then 3 floats,
    // 5 floats (20 bytes) apart. Attribute indices match "layout(location=N)"
    // in the vertex shader.
    const gl::VertexAttrib layout[] = {
        {gl::VertexAttribType::FLOAT, 2, 0, false}, // a_position
        {gl::VertexAttribType::FLOAT, 3, 0, false}, // a_color
    };
    gl::VertexArray vao;
    vao.AddVertexBuffer(vbo, layout, 2, 5 * sizeof(float));

    gl::Renderer::ClearColor(0.08f, 0.08f, 0.12f, 1.0f);

    // STEP 5: the frame loop. Bind the shader, bind the VAO, draw.
    int frame = 0;
    while (app.PollEvents())
    {
        app.BeginFrame();
        gl::Renderer::Clear(true, false);

        shader.Bind();
        vao.Bind();
        gl::Renderer::Draw(gl::RenderPrimitive::TRIANGLES, 3);

        app.EndFrame();
        ++frame;
        if (maxFrames > 0 && frame >= maxFrames) break;
    }

    shader.Release();
    vbo.Release();
    vao.Release();
    app.Destroy();
    return 0;
}
