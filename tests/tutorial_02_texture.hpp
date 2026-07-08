#pragma once

// TUTORIAL 2 — Textured Quad
//
// Builds on tutorial 1: adds a Texture, and switches from Draw() (raw
// vertex list) to DrawIndexed() (vertices + an index buffer). A quad is two
// triangles that share two vertices — indices avoid storing those corners
// twice, and are the normal way to draw anything bigger than a triangle.

#include "test_common.hpp"
#include "wabbit_sprite.h" // embedded 32x32 RGBA8 pixels — no file loading needed

static const char* kTutorialTextureVS = R"(#version 430 core
layout(location = 0) in vec2 a_position;
layout(location = 1) in vec2 a_uv;
out vec2 v_uv;
void main()
{
    gl_Position = vec4(a_position, 0.0, 1.0);
    v_uv = a_uv;
}
)";

static const char* kTutorialTextureFS = R"(#version 430 core
in vec2 v_uv;
out vec4 OutColor;
uniform sampler2D u_tex;
void main()
{
    OutColor = texture(u_tex, v_uv);
}
)";

inline int tutorial_02_texture(int maxFrames)
{
    TestApp app;
    if (!app.Create("tutorial 02 - texture")) return 1;

    gl::Shader shader;
    if (!shader.LoadFromString(gl::PipelineStage::VERTEX, kTutorialTextureVS) ||
        !shader.LoadFromString(gl::PipelineStage::FRAGMENT, kTutorialTextureFS) || !shader.Link())
    {
        fprintf(stderr, "shader error: %s\n", shader.GetLog());
        app.Destroy();
        return 1;
    }

    // 4 corners (x,y,u,v) — one entry per corner, not per triangle.
    const float vertices[] = {
        -0.5f, -0.5f, 0.0f, 0.0f, // bottom-left
        0.5f,  -0.5f, 1.0f, 0.0f, // bottom-right
        0.5f,  0.5f,  1.0f, 1.0f, // top-right
        -0.5f, 0.5f,  0.0f, 1.0f, // top-left
    };
    // two triangles reusing corners 0 and 2
    const gl::u16 indices[] = {0, 1, 2, 0, 2, 3};

    gl::Buffer vbo, ibo;
    vbo.Allocate(gl::BufferType::ARRAY, vertices, sizeof(vertices), gl::UsageType::STATIC_DRAW);
    ibo.Allocate(gl::BufferType::ELEMENT_ARRAY, indices, sizeof(indices),
                 gl::UsageType::STATIC_DRAW);

    const gl::VertexAttrib layout[] = {
        {gl::VertexAttribType::FLOAT, 2, 0, false}, // a_position
        {gl::VertexAttribType::FLOAT, 2, 0, false}, // a_uv
    };
    gl::VertexArray vao;
    vao.AddVertexBuffer(vbo, layout, 2, 4 * sizeof(float));
    vao.SetIndexBuffer(ibo, gl::VertexAttribType::USHORT); // must match the index type above

    // the texture: 32x32 RGBA8 pixels baked into wabbit_sprite.h at build time
    gl::Texture tex;
    tex.Load2D(kWabbitPixels, 32, 32, gl::TextureFormat::RGBA8);
    tex.SetFilter(gl::TextureFilter::NEAREST, gl::TextureFilter::NEAREST); // crisp pixel art

    shader.SetInt("u_tex", 0); // sampler2D u_tex reads from texture unit 0

    gl::Renderer::ClearColor(0.08f, 0.08f, 0.12f, 1.0f);
    gl::Renderer::SetBlend(true);
    gl::Renderer::SetBlendFactors(gl::BlendFactor::SRC_ALPHA, gl::BlendFactor::ONE_MINUS_SRC_ALPHA);

    int frame = 0;
    while (app.PollEvents())
    {
        app.BeginFrame();
        gl::Renderer::Clear(true, false);

        shader.Bind();
        tex.Bind(0); // unit 0 — matches SetInt("u_tex", 0) above
        vao.Bind();
        gl::Renderer::DrawIndexed(gl::RenderPrimitive::TRIANGLES, 6);

        app.EndFrame();
        ++frame;
        if (maxFrames > 0 && frame >= maxFrames) break;
    }

    tex.Release();
    shader.Release();
    vbo.Release();
    ibo.Release();
    vao.Release();
    app.Destroy();
    return 0;
}
