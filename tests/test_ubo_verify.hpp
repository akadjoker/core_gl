#pragma once

// Verifies Shader::BindUniformBlock(): two different shader programs (own
// vertex stage, own position) both declare the same "ColorBlock" uniform
// block and are bound to the same UBO binding point. A single
// Buffer::Upload() updates the color for BOTH shaders — neither is ever
// touched with a per-shader color uniform. Checked pixel-exact, twice
// (before and after changing the shared buffer), proving the data really
// flows through the buffer and not through some per-shader fallback.

#include "test_common.hpp"

// left-half quad, reads u_color from the shared UBO
static const char* kUboLeftVS = R"(#version 430 core
layout(location = 0) in vec2 a_position;
void main()
{
    gl_Position = vec4(a_position, 0.0, 1.0);
}
)";

// right-half quad, different vertex shader/program entirely — only the UBO
// binding is shared, not the program
static const char* kUboRightVS = R"(#version 430 core
layout(location = 0) in vec2 a_position;
void main()
{
    gl_Position = vec4(a_position, 0.0, 1.0);
}
)";

static const char* kUboFS = R"(#version 430 core
layout(std140) uniform ColorBlock
{
    vec4 u_color;
};
out vec4 OutColor;
void main()
{
    OutColor = u_color;
}
)";

inline int test_ubo_verify(int /*maxFrames*/)
{
    TestApp app;
    if (!app.Create("coregl - UBO verify", 320, 240)) return 1;

    const int S = 256;
    gl::Texture color;
    color.Load2D(nullptr, S, S, gl::TextureFormat::RGBA8);
    gl::FrameBuffer fbo;
    fbo.AttachTexture(color, gl::Attachment::COLOR0);
    fbo.SetDrawBuffers();
    if (!fbo.IsComplete())
    {
        fprintf(stderr, "FAIL: framebuffer incomplete\n");
        app.Destroy();
        return 1;
    }

    gl::Shader shaderLeft, shaderRight;
    if (!shaderLeft.LoadFromString(gl::PipelineStage::VERTEX, kUboLeftVS) ||
        !shaderLeft.LoadFromString(gl::PipelineStage::FRAGMENT, kUboFS) || !shaderLeft.Link() ||
        !shaderRight.LoadFromString(gl::PipelineStage::VERTEX, kUboRightVS) ||
        !shaderRight.LoadFromString(gl::PipelineStage::FRAGMENT, kUboFS) || !shaderRight.Link())
    {
        fprintf(stderr, "shader error: %s%s\n", shaderLeft.GetLog(), shaderRight.GetLog());
        app.Destroy();
        return 1;
    }

    const gl::u32 kColorBinding = 0;
    shaderLeft.BindUniformBlock("ColorBlock", kColorBinding);
    shaderRight.BindUniformBlock("ColorBlock", kColorBinding);

    // std140 layout of `{ vec4 u_color; }` is just 16 bytes, no padding surprises
    struct ColorBlock
    {
        float r, g, b, a;
    };
    gl::Buffer ubo;
    ColorBlock initial = {1.f, 0.f, 0.f, 1.f}; // red
    ubo.Allocate(gl::BufferType::UNIFORM, &initial, sizeof(ColorBlock),
                 gl::UsageType::DYNAMIC_DRAW);
    ubo.BindBase(kColorBinding);

    // left quad: x in [-1, -0.1]; right quad: x in [0.1, 1] — leaves a gap so
    // there's no seam ambiguity when sampling near the center
    const float leftVerts[] = {-1.f, -1.f, -0.1f, -1.f, -0.1f, 1.f, -1.f, 1.f};
    const float rightVerts[] = {0.1f, -1.f, 1.f, -1.f, 1.f, 1.f, 0.1f, 1.f};
    const gl::u16 quadIndices[] = {0, 1, 2, 0, 2, 3};

    gl::Buffer leftVbo, rightVbo, ibo;
    leftVbo.Allocate(gl::BufferType::ARRAY, leftVerts, sizeof(leftVerts),
                     gl::UsageType::STATIC_DRAW);
    rightVbo.Allocate(gl::BufferType::ARRAY, rightVerts, sizeof(rightVerts),
                      gl::UsageType::STATIC_DRAW);
    ibo.Allocate(gl::BufferType::ELEMENT_ARRAY, quadIndices, sizeof(quadIndices),
                 gl::UsageType::STATIC_DRAW);

    const gl::VertexAttrib layout[] = {{gl::VertexAttribType::FLOAT, 2, 0, false}};
    gl::VertexArray leftVao, rightVao;
    leftVao.AddVertexBuffer(leftVbo, layout, 1, 2 * sizeof(float));
    leftVao.SetIndexBuffer(ibo, gl::VertexAttribType::USHORT);
    rightVao.AddVertexBuffer(rightVbo, layout, 1, 2 * sizeof(float));
    rightVao.SetIndexBuffer(ibo, gl::VertexAttribType::USHORT);

    fbo.Bind();
    gl::Renderer::Viewport(0, 0, S, S);
    gl::Renderer::SetDepthTest(false);
    gl::Renderer::SetBlend(false);
    gl::Renderer::ClearColor(0.f, 0.f, 0.f, 1.f);

    auto drawBoth = [&]()
    {
        gl::Renderer::Clear(true, false);
        shaderLeft.Bind();
        leftVao.Bind();
        gl::Renderer::DrawIndexed(gl::RenderPrimitive::TRIANGLES, 6);
        shaderRight.Bind();
        rightVao.Bind();
        gl::Renderer::DrawIndexed(gl::RenderPrimitive::TRIANGLES, 6);
    };

    gl::u8* px = new gl::u8[S * S * 4];
    int failed = 0;

    // --- round 1: initial red, uploaded once before either shader ran ---
    drawBoth();
    gl::Renderer::ReadPixels(0, 0, S, S, px);
    {
        const gl::u8* left = &px[(128 * S + 64) * 4];
        const gl::u8* right = &px[(128 * S + 192) * 4];
        bool ok = left[0] == 255 && left[1] == 0 && left[2] == 0 && right[0] == 255 &&
                  right[1] == 0 && right[2] == 0;
        printf("round 1 (red)   left=(%u,%u,%u) right=(%u,%u,%u)  %s\n", left[0], left[1], left[2],
               right[0], right[1], right[2], ok ? "OK" : "FAIL");
        if (!ok) ++failed;
    }

    // --- round 2: update the SHARED buffer only — neither shader is touched ---
    ColorBlock updated = {0.f, 1.f, 0.f, 1.f}; // green
    ubo.Upload(&updated, sizeof(ColorBlock));
    drawBoth();
    gl::Renderer::ReadPixels(0, 0, S, S, px);
    {
        const gl::u8* left = &px[(128 * S + 64) * 4];
        const gl::u8* right = &px[(128 * S + 192) * 4];
        bool ok = left[0] == 0 && left[1] == 255 && left[2] == 0 && right[0] == 0 &&
                  right[1] == 255 && right[2] == 0;
        printf("round 2 (green) left=(%u,%u,%u) right=(%u,%u,%u)  %s\n", left[0], left[1], left[2],
               right[0], right[1], right[2],
               ok ? "OK (both shaders followed the buffer, untouched individually)" : "FAIL");
        if (!ok) ++failed;
    }

    delete[] px;
    printf(failed == 0 ? "ALL CHECKS PASSED\n" : "%d CHECKS FAILED\n", failed);

    shaderLeft.Release();
    shaderRight.Release();
    app.Destroy();
    return failed == 0 ? 0 : 1;
}
