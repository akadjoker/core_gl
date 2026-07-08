#pragma once

// Test: render-to-texture — the rotating triangle is drawn into an offscreen
// 512x512 framebuffer, then a fullscreen post-process pass samples that
// texture with a wave distortion + vignette. This is the exact pattern every
// deferred / post-processing pass will use.

#include "test_common.hpp"
#include <cmath>

static const char* kSceneVS = R"(#version 430 core
layout(location = 0) in vec2 position;
layout(location = 1) in vec3 color;
uniform float u_angle;
out vec3 v_color;
void main()
{
    float c = cos(u_angle), s = sin(u_angle);
    gl_Position = vec4(mat2(c, s, -s, c) * position, 0.0, 1.0);
    v_color = color;
}
)";

static const char* kSceneFS = R"(#version 430 core
in vec3 v_color;
out vec4 OutColor;
void main()
{
    OutColor = vec4(v_color, 1.0);
}
)";

// fullscreen triangle from gl_VertexID — no VBO needed
static const char* kPostVS = R"(#version 430 core
out vec2 TexCoord;
void main()
{
    vec2 uv = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
    TexCoord = uv;
    gl_Position = vec4(uv * 2.0 - 1.0, 0.0, 1.0);
}
)";

static const char* kPostFS = R"(#version 430 core
in vec2 TexCoord;
out vec4 OutColor;
uniform sampler2D u_scene;
uniform float u_time;
void main()
{
    vec2 uv = TexCoord + 0.01 * vec2(sin(u_time * 3.0 + TexCoord.y * 20.0), 0.0);
    vec3 color = texture(u_scene, uv).rgb;
    float d = distance(TexCoord, vec2(0.5));
    color *= 1.0 - 0.6 * d * d; // vignette
    OutColor = vec4(color, 1.0);
}
)";

inline int test_rendertarget(int maxFrames)
{
    TestApp app;
    if (!app.Create("coregl - render to texture")) return 1;

    gl::Shader scene, post;
    if (!scene.LoadFromString(gl::PipelineStage::VERTEX, kSceneVS) ||
        !scene.LoadFromString(gl::PipelineStage::FRAGMENT, kSceneFS) || !scene.Link() ||
        !post.LoadFromString(gl::PipelineStage::VERTEX, kPostVS) ||
        !post.LoadFromString(gl::PipelineStage::FRAGMENT, kPostFS) || !post.Link())
    {
        fprintf(stderr, "shader error: %s%s\n", scene.GetLog(), post.GetLog());
        app.Destroy();
        return 1;
    }

    const float verts[] = {
        0.0f,  0.6f,  1.0f, 0.2f, 0.2f, //
        -0.6f, -0.6f, 0.2f, 1.0f, 0.2f, //
        0.6f,  -0.6f, 0.2f, 0.2f, 1.0f, //
    };
    gl::Buffer vbo;
    vbo.Allocate(gl::BufferType::ARRAY, verts, sizeof(verts), gl::UsageType::STATIC_DRAW);

    const gl::VertexAttrib layout[] = {
        {gl::VertexAttribType::FLOAT, 2, 0, false},
        {gl::VertexAttribType::FLOAT, 3, 0, false},
    };
    gl::VertexArray vao;
    vao.AddVertexBuffer(vbo, layout, 2, 5 * sizeof(float));

    gl::VertexArray emptyVao; // for the fullscreen gl_VertexID triangle

    // offscreen target: color + depth
    const int kSize = 512;
    gl::Texture colorTex, depthTex;
    colorTex.Load2D(nullptr, kSize, kSize, gl::TextureFormat::RGBA8);
    depthTex.LoadDepth(kSize, kSize);

    gl::FrameBuffer fbo;
    fbo.AttachTexture(colorTex, gl::Attachment::COLOR0);
    fbo.AttachTexture(depthTex, gl::Attachment::DEPTH);
    fbo.SetDrawBuffers();
    if (!fbo.IsComplete())
    {
        fprintf(stderr, "framebuffer incomplete\n");
        app.Destroy();
        return 1;
    }
    printf("offscreen framebuffer %dx%d complete\n", kSize, kSize);

    const gl::i32 angleLoc = scene.GetLocation("u_angle");
    const gl::i32 timeLoc = post.GetLocation("u_time");
    post.SetInt("u_scene", 0);

    gl::Renderer::SetDepthTest(true);

    int frame = 0;
    while (app.PollEvents())
    {
        float t = (float)frame / 60.0f;

        // pass 1: scene into the offscreen framebuffer
        fbo.Bind();
        gl::Renderer::Viewport(0, 0, kSize, kSize);
        gl::Renderer::ClearColor(0.08f, 0.08f, 0.12f, 1.0f);
        gl::Renderer::Clear(true, true);

        scene.Bind();
        scene.SetFloat(angleLoc, t);
        vao.Bind();
        gl::Renderer::Draw(gl::RenderPrimitive::TRIANGLES, 3);

        // pass 2: post-process to the screen
        gl::Renderer::BindScreen();
        app.BeginFrame(); // window viewport
        gl::Renderer::Clear(true, true);

        post.Bind();
        post.SetFloat(timeLoc, t);
        colorTex.Bind(0);
        emptyVao.Bind();
        gl::Renderer::Draw(gl::RenderPrimitive::TRIANGLES, 3);

        app.EndFrame();
        ++frame;
        if (maxFrames > 0 && frame >= maxFrames) break;
    }

    const gl::RenderStats& st = gl::Renderer::GetStats();
    printf("frames: %d | draw calls: %llu | fbo switches: %llu | texture binds: %llu\n", frame,
           (unsigned long long)st.drawCalls, (unsigned long long)st.fboSwitches,
           (unsigned long long)st.textureBinds);

    app.Destroy();
    return 0;
}
