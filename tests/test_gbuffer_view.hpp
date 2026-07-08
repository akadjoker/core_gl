#pragma once

// G-buffer style debug view: a geometry pass writes albedo + a second
// "normal-like" buffer via MRT into an offscreen target (plus a real depth
// attachment), then the composite pass tiles all three as quads on screen —
// one big view and two thumbnails — using Renderer::DrawQuad(). Viewport is
// set ONCE for the whole composite pass; each quad is placed purely by its
// own u_rect uniform. This is the pattern for any multi-view debug HUD
// (deferred G-buffer, shadow atlas, shadow cascades, etc).

#include "test_common.hpp"
#include <cmath>

static const char* kGBufferVS = R"(#version 430 core
layout(location = 0) in vec2 position;
layout(location = 1) in vec3 color;
uniform vec2 u_pos;
uniform float u_depth;
uniform float u_angle;
out vec3 v_color;
void main()
{
    float c = cos(u_angle), s = sin(u_angle);
    vec2 p = mat2(c, s, -s, c) * position;
    gl_Position = vec4(p + u_pos, u_depth, 1.0);
    v_color = color;
}
)";

// MRT: two color outputs from a single pass — the classic G-buffer shape.
static const char* kGBufferFS = R"(#version 430 core
in vec3 v_color;
layout(location = 0) out vec4 oAlbedo;
layout(location = 1) out vec4 oNormal;
void main()
{
    oAlbedo = vec4(v_color, 1.0);
    oNormal = vec4(1.0 - v_color, 1.0); // stand-in for a second G-buffer channel
}
)";

// Composite: samples one of the G-buffer textures into a positioned quad.
static const char* kBlitFS = R"(#version 430 core
in vec2 v_uv;
out vec4 OutColor;
uniform sampler2D u_tex;
uniform int u_isDepth;
void main()
{
    // render targets are stored bottom-up (GL convention); DrawQuad's v_uv is
    // top-left-down (UI convention) — flip on read so thumbnails aren't upside down
    vec2 uv = vec2(v_uv.x, 1.0 - v_uv.y);
    if (u_isDepth != 0)
    {
        float d = texture(u_tex, uv).r;
        OutColor = vec4(vec3(d), 1.0);
    }
    else
    {
        OutColor = texture(u_tex, uv);
    }
}
)";

inline int test_gbuffer_view(int maxFrames)
{
    TestApp app;
    if (!app.Create("coregl - G-buffer view", 960, 600)) return 1;

    gl::Shader gbuffer, blit;
    if (!gbuffer.LoadFromString(gl::PipelineStage::VERTEX, kGBufferVS) ||
        !gbuffer.LoadFromString(gl::PipelineStage::FRAGMENT, kGBufferFS) || !gbuffer.Link() ||
        !blit.LoadFromString(gl::PipelineStage::VERTEX, gl::Renderer::QuadShaderSource()) ||
        !blit.LoadFromString(gl::PipelineStage::FRAGMENT, kBlitFS) || !blit.Link())
    {
        fprintf(stderr, "shader error: %s%s\n", gbuffer.GetLog(), blit.GetLog());
        app.Destroy();
        return 1;
    }

    // two shapes at different depths so the depth thumbnail is meaningful
    const float triA[] = {
        0.0f,  0.35f, 1.0f, 0.3f, 0.2f, //
        -0.3f, -0.3f, 0.2f, 1.0f, 0.3f, //
        0.3f,  -0.3f, 0.2f, 0.3f, 1.0f, //
    };
    const float triB[] = {
        0.0f,   0.5f,  0.9f, 0.9f, 0.2f, //
        -0.45f, -0.4f, 0.2f, 0.9f, 0.9f, //
        0.45f,  -0.4f, 0.9f, 0.2f, 0.9f, //
    };
    gl::Buffer vboA, vboB;
    vboA.Allocate(gl::BufferType::ARRAY, triA, sizeof(triA), gl::UsageType::STATIC_DRAW);
    vboB.Allocate(gl::BufferType::ARRAY, triB, sizeof(triB), gl::UsageType::STATIC_DRAW);
    const gl::VertexAttrib layout[] = {
        {gl::VertexAttribType::FLOAT, 2, 0, false},
        {gl::VertexAttribType::FLOAT, 3, 0, false},
    };
    gl::VertexArray vaoA, vaoB;
    vaoA.AddVertexBuffer(vboA, layout, 2, 5 * sizeof(float));
    vaoB.AddVertexBuffer(vboB, layout, 2, 5 * sizeof(float));

    // offscreen G-buffer: albedo + "normal" + real depth
    const int kSize = 512;
    gl::Texture albedoTex, normalTex, depthTex;
    albedoTex.Load2D(nullptr, kSize, kSize, gl::TextureFormat::RGBA8);
    normalTex.Load2D(nullptr, kSize, kSize, gl::TextureFormat::RGBA8);
    depthTex.LoadDepth(kSize, kSize);

    gl::FrameBuffer gbufferFbo;
    gbufferFbo.AttachTexture(albedoTex, gl::Attachment::COLOR0);
    gbufferFbo.AttachTexture(normalTex, gl::Attachment::COLOR1);
    gbufferFbo.AttachTexture(depthTex, gl::Attachment::DEPTH);
    gbufferFbo.SetDrawBuffers();
    if (!gbufferFbo.IsComplete())
    {
        fprintf(stderr, "G-buffer framebuffer incomplete\n");
        app.Destroy();
        return 1;
    }
    printf("G-buffer %dx%d, 2 color attachments + depth\n", kSize, kSize);

    const gl::i32 gPosLoc = gbuffer.GetLocation("u_pos");
    const gl::i32 gDepthLoc = gbuffer.GetLocation("u_depth");
    const gl::i32 gAngleLoc = gbuffer.GetLocation("u_angle");

    const gl::i32 bRectLoc = blit.GetLocation("u_rect");
    const gl::i32 bSizeLoc = blit.GetLocation("u_targetSize");
    const gl::i32 bDepthFlagLoc = blit.GetLocation("u_isDepth");
    blit.SetInt("u_tex", 0);

    gl::Batch batch; // just for the on-screen labels
    if (!batch.Init(4096))
    {
        fprintf(stderr, "batch init failed\n");
        app.Destroy();
        return 1;
    }

    int frame = 0;
    while (app.PollEvents())
    {
        float t = (float)frame / 60.0f;

        // --- pass 1: geometry into the G-buffer (MRT) ---
        gbufferFbo.Bind();
        gl::Renderer::Viewport(0, 0, kSize, kSize);
        gl::Renderer::SetDepthTest(true);
        gl::Renderer::ClearColor(0.05f, 0.05f, 0.08f, 1.f);
        gl::Renderer::Clear(true, true);

        gbuffer.Bind();
        gbuffer.SetVec2(gPosLoc, 0.2f * sinf(t), 0.f);
        gbuffer.SetFloat(gDepthLoc, 0.6f); // far
        gbuffer.SetFloat(gAngleLoc, t);
        vaoA.Bind();
        gl::Renderer::Draw(gl::RenderPrimitive::TRIANGLES, 3);

        gbuffer.SetVec2(gPosLoc, -0.15f * sinf(t * 1.3f), 0.1f);
        gbuffer.SetFloat(gDepthLoc, -0.2f); // near
        gbuffer.SetFloat(gAngleLoc, -t * 0.7f);
        vaoB.Bind();
        gl::Renderer::Draw(gl::RenderPrimitive::TRIANGLES, 3);

        // --- pass 2: composite to the screen — one Viewport, several DrawQuad ---
        gl::Renderer::BindScreen();
        app.BeginFrame();
        int w, h;
        SDL_GL_GetDrawableSize(app.window, &w, &h);
        gl::Renderer::SetDepthTest(false);
        gl::Renderer::ClearColor(0.02f, 0.02f, 0.03f, 1.f);
        gl::Renderer::Clear(true, false);

        blit.Bind();
        blit.SetVec2(bSizeLoc, (float)w, (float)h);

        struct View
        {
            float x, y, w, h;
            gl::Texture* tex;
            bool isDepth;
        };
        const float pad = 20.f;
        const float bigSize = (float)h - 2.f * pad;
        const float thumb = (bigSize - pad) * 0.5f;
        const View views[3] = {
            {pad, pad, bigSize, bigSize, &albedoTex, false},
            {pad * 2.f + bigSize, pad, thumb, thumb, &normalTex, false},
            {pad * 2.f + bigSize, pad * 2.f + thumb, thumb, thumb, &depthTex, true},
        };
        const char* labels[3] = {"ALBEDO", "NORMAL", "DEPTH"};

        for (int i = 0; i < 3; ++i)
        {
            blit.SetVec4(bRectLoc, views[i].x, views[i].y, views[i].w, views[i].h);
            blit.SetInt(bDepthFlagLoc, views[i].isDepth ? 1 : 0);
            views[i].tex->Bind(0);
            gl::Renderer::DrawQuad(); // no Viewport() call between these three
        }

        for (int i = 0; i < 3; ++i)
        {
            batch.SetColor(255, 255, 255, 255);
            batch.Text(views[i].x + 6.f, views[i].y + 6.f, 14.f, labels[i]);
        }
        char hud[64];
        snprintf(hud, sizeof(hud), "draw calls: %llu",
                 (unsigned long long)gl::Renderer::GetStats().drawCalls);
        batch.Text(pad, (float)h - 24.f, 14.f, hud);
        batch.Render();

        app.EndFrame();
        ++frame;
        if (maxFrames > 0 && frame >= maxFrames) break;
    }

    const gl::RenderStats& st = gl::Renderer::GetStats();
    printf("frames: %d | draw calls: %llu | fbo switches: %llu\n", frame,
           (unsigned long long)st.drawCalls, (unsigned long long)st.fboSwitches);

    batch.Release();
    gbuffer.Release();
    blit.Release();
    app.Destroy();
    return 0;
}
