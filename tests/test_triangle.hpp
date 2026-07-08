#pragma once

// Test: colored triangle — exercises Shader, Buffer, VertexArray and Draw.

#include "test_common.hpp"
#include <cmath>

static const char* kTriangleVS = R"(#version 430 core
layout(location = 0) in vec2 position;
layout(location = 1) in vec3 color;
uniform float u_angle;
out vec3 v_color;
void main()
{
    float c = cos(u_angle), s = sin(u_angle);
    vec2 p = mat2(c, s, -s, c) * position;
    gl_Position = vec4(p, 0.0, 1.0);
    v_color = color;
}
)";

static const char* kTriangleFS = R"(#version 430 core
in vec3 v_color;
out vec4 OutColor;
void main()
{
    OutColor = vec4(v_color, 1.0);
}
)";

inline int test_triangle(int maxFrames)
{
    TestApp app;
    if (!app.Create("coregl - triangle")) return 1;

    gl::Shader shader;
    if (!shader.LoadFromString(gl::PipelineStage::VERTEX, kTriangleVS) ||
        !shader.LoadFromString(gl::PipelineStage::FRAGMENT, kTriangleFS) || !shader.Link())
    {
        fprintf(stderr, "shader error: %s\n", shader.GetLog());
        app.Destroy();
        return 1;
    }

    // pos.xy + color.rgb, interleaved
    const float verts[] = {
        0.0f,  0.6f,  1.0f, 0.2f, 0.2f, // top (red)
        -0.6f, -0.6f, 0.2f, 1.0f, 0.2f, // bottom left (green)
        0.6f,  -0.6f, 0.2f, 0.2f, 1.0f, // bottom right (blue)
    };
    gl::Buffer vbo;
    vbo.Allocate(gl::BufferType::ARRAY, verts, sizeof(verts), gl::UsageType::STATIC_DRAW);

    const gl::VertexAttrib layout[] = {
        {gl::VertexAttribType::FLOAT, 2, 0, false}, // position
        {gl::VertexAttribType::FLOAT, 3, 0, false}, // color
    };
    gl::VertexArray vao;
    vao.AddVertexBuffer(vbo, layout, 2, 5 * sizeof(float));

    gl::Renderer::ClearColor(0.08f, 0.08f, 0.12f, 1.0f);

    // hot path: resolve the uniform location once, set by location per frame
    const gl::i32 angleLoc = shader.GetLocation("u_angle");

    int frame = 0;
    while (app.PollEvents())
    {
        app.BeginFrame();
        gl::Renderer::Clear(true, true);

        shader.Bind();
        shader.SetFloat(angleLoc, (float)frame / 120.0f);
        vao.Bind();
        gl::Renderer::Draw(gl::RenderPrimitive::TRIANGLES, 3);

        app.EndFrame();
        ++frame;
        if (maxFrames > 0 && frame >= maxFrames) break;
    }

    const gl::RenderStats& st = gl::Renderer::GetStats();
    printf("frames: %d | draw calls: %llu | shader switches: %llu | vao switches: %llu\n", frame,
           (unsigned long long)st.drawCalls, (unsigned long long)st.shaderSwitches,
           (unsigned long long)st.vaoSwitches);

    app.Destroy();
    return 0;
}
