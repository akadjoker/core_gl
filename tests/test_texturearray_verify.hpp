#pragma once

// Verifies Texture::LoadArray + sampler2DArray: uploads 4 layers, each a
// distinct solid color, then samples each layer with a shader that picks
// the layer via a uniform, checking pixel-exact output per layer. Also
// verifies FrameBuffer::AttachTextureLayer by rendering into one layer of an
// empty color array (the render-target usage cascaded shadow maps rely on)
// and reading that layer back through the same array sampler.

#include "test_common.hpp"

static const char* kArraySampleFS = R"(#version 430 core
in vec2 v_uv;
out vec4 OutColor;
uniform sampler2DArray u_tex;
uniform int u_layer;
void main()
{
    OutColor = texture(u_tex, vec3(v_uv, float(u_layer)));
}
)";

inline int test_texturearray_verify(int /*maxFrames*/)
{
    TestApp app;
    if (!app.Create("coregl - texture array verify", 320, 240)) return 1;

    const int S = 64;
    const int kLayers = 4;

    // 4 layers, each one flat color, packed back to back
    gl::u8 pixels[kLayers][S * S * 4];
    const gl::u8 layerColor[kLayers][3] = {{255, 0, 0}, {0, 255, 0}, {0, 0, 255}, {255, 255, 0}};
    for (int l = 0; l < kLayers; ++l)
        for (int i = 0; i < S * S; ++i)
        {
            pixels[l][i * 4 + 0] = layerColor[l][0];
            pixels[l][i * 4 + 1] = layerColor[l][1];
            pixels[l][i * 4 + 2] = layerColor[l][2];
            pixels[l][i * 4 + 3] = 255;
        }
    // LoadArray expects the layers contiguous in memory — pixels[][] already is
    gl::Texture arrayTex;
    arrayTex.LoadArray(pixels, S, S, kLayers, gl::TextureFormat::RGBA8);
    arrayTex.SetFilter(gl::TextureFilter::NEAREST, gl::TextureFilter::NEAREST);

    gl::Texture target;
    target.Load2D(nullptr, S, S, gl::TextureFormat::RGBA8);
    gl::FrameBuffer fbo;
    fbo.AttachTexture(target, gl::Attachment::COLOR0);
    fbo.SetDrawBuffers();
    if (!fbo.IsComplete())
    {
        fprintf(stderr, "FAIL: framebuffer incomplete\n");
        app.Destroy();
        return 1;
    }

    gl::Shader shader;
    if (!shader.LoadFromString(gl::PipelineStage::VERTEX, gl::Renderer::QuadShaderSource()) ||
        !shader.LoadFromString(gl::PipelineStage::FRAGMENT, kArraySampleFS) || !shader.Link())
    {
        fprintf(stderr, "shader error: %s\n", shader.GetLog());
        app.Destroy();
        return 1;
    }
    const gl::i32 rectLoc = shader.GetLocation("u_rect");
    const gl::i32 sizeLoc = shader.GetLocation("u_targetSize");
    const gl::i32 layerLoc = shader.GetLocation("u_layer");
    shader.SetInt("u_tex", 0);

    fbo.Bind();
    gl::Renderer::Viewport(0, 0, S, S);
    gl::Renderer::SetDepthTest(false);
    gl::Renderer::SetBlend(false);

    gl::u8* px = new gl::u8[S * S * 4];
    int failed = 0;

    for (int l = 0; l < kLayers; ++l)
    {
        gl::Renderer::Clear(true, false);
        shader.Bind();
        shader.SetVec4(rectLoc, 0.f, 0.f, (float)S, (float)S);
        shader.SetVec2(sizeLoc, (float)S, (float)S);
        shader.SetInt(layerLoc, l);
        arrayTex.Bind(0);
        gl::Renderer::DrawQuad();

        gl::Renderer::ReadPixels(0, 0, S, S, px);
        const gl::u8* p = &px[(S / 2 * S + S / 2) * 4];
        bool ok = p[0] == layerColor[l][0] && p[1] == layerColor[l][1] && p[2] == layerColor[l][2];
        printf("layer %d: got (%3u,%3u,%3u) expected (%3u,%3u,%3u)  %s\n", l, p[0], p[1], p[2],
               layerColor[l][0], layerColor[l][1], layerColor[l][2], ok ? "OK" : "FAIL");
        if (!ok) ++failed;
    }

    // --- render target usage: attach layer 2 of an empty array, draw solid
    // magenta into it, then sample the SAME array at layer 2 to confirm the
    // render actually landed on the right slice ---
    gl::Texture rtArray;
    rtArray.LoadArray(nullptr, S, S, kLayers, gl::TextureFormat::RGBA8);
    rtArray.SetFilter(gl::TextureFilter::NEAREST, gl::TextureFilter::NEAREST);

    gl::FrameBuffer layerFbo;
    layerFbo.AttachTextureLayer(rtArray, gl::Attachment::COLOR0, 2);
    layerFbo.SetDrawBuffers();
    if (!layerFbo.IsComplete())
    {
        fprintf(stderr, "FAIL: layer framebuffer incomplete\n");
        delete[] px;
        app.Destroy();
        return 1;
    }
    layerFbo.Bind();
    gl::Renderer::Viewport(0, 0, S, S);
    gl::Renderer::ClearColor(1.f, 0.f, 1.f, 1.f); // magenta
    gl::Renderer::Clear(true, false);

    fbo.Bind();
    gl::Renderer::Viewport(0, 0, S, S);
    gl::Renderer::Clear(true, false);
    shader.Bind();
    shader.SetVec4(rectLoc, 0.f, 0.f, (float)S, (float)S);
    shader.SetVec2(sizeLoc, (float)S, (float)S);
    shader.SetInt(layerLoc, 2);
    rtArray.Bind(0);
    gl::Renderer::DrawQuad();
    gl::Renderer::ReadPixels(0, 0, S, S, px);
    {
        const gl::u8* p = &px[(S / 2 * S + S / 2) * 4];
        bool ok = p[0] == 255 && p[1] == 0 && p[2] == 255;
        printf("render-to-layer 2: got (%3u,%3u,%3u) expected (255,  0,255)  %s\n", p[0], p[1],
               p[2], ok ? "OK" : "FAIL");
        if (!ok) ++failed;
    }
    // an untouched layer (0) must still read as transparent/black, proving
    // the render-to-layer call didn't leak into every slice
    shader.SetInt(layerLoc, 0);
    gl::Renderer::Clear(true, false);
    gl::Renderer::DrawQuad();
    gl::Renderer::ReadPixels(0, 0, S, S, px);
    {
        const gl::u8* p = &px[(S / 2 * S + S / 2) * 4];
        bool ok = p[0] == 0 && p[1] == 0 && p[2] == 0;
        printf("untouched layer 0: got (%3u,%3u,%3u) expected (  0,  0,  0)  %s\n", p[0], p[1],
               p[2], ok ? "OK" : "FAIL");
        if (!ok) ++failed;
    }

    delete[] px;
    printf(failed == 0 ? "ALL CHECKS PASSED\n" : "%d CHECKS FAILED\n", failed);

    shader.Release();
    app.Destroy();
    return failed == 0 ? 0 : 1;
}
