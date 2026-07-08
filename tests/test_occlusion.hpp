#pragma once

// Test: hardware occlusion query — a wall slides in front of a triangle.
// The triangle draw is wrapped in a SAMPLES_PASSED query; when the wall covers
// it the GPU reports 0 samples. Two queries ping-pong so reading the result
// never stalls the pipeline.

#include "test_common.hpp"
#include <cmath>

static const char* kOcclusionVS = R"(#version 430 core
layout(location = 0) in vec2 position;
layout(location = 1) in vec3 color;
uniform vec2 u_pos;
uniform float u_depth;
out vec3 v_color;
void main()
{
    gl_Position = vec4(position + u_pos, u_depth, 1.0);
    v_color = color;
}
)";

static const char* kOcclusionFS = R"(#version 430 core
in vec3 v_color;
out vec4 OutColor;
void main()
{
    OutColor = vec4(v_color, 1.0);
}
)";

inline int test_occlusion(int maxFrames)
{
    TestApp app;
    if (!app.Create("coregl - occlusion query")) return 1;

    gl::Shader shader;
    if (!shader.LoadFromString(gl::PipelineStage::VERTEX, kOcclusionVS) ||
        !shader.LoadFromString(gl::PipelineStage::FRAGMENT, kOcclusionFS) || !shader.Link())
    {
        fprintf(stderr, "shader error: %s\n", shader.GetLog());
        app.Destroy();
        return 1;
    }

    // triangle (colored)
    const float triVerts[] = {
        0.0f,  0.4f,  1.0f, 0.3f, 0.2f, //
        -0.4f, -0.4f, 0.2f, 1.0f, 0.3f, //
        0.4f,  -0.4f, 0.2f, 0.3f, 1.0f, //
    };
    gl::Buffer triVbo;
    triVbo.Allocate(gl::BufferType::ARRAY, triVerts, sizeof(triVerts), gl::UsageType::STATIC_DRAW);

    // wall (dark grey quad, 2 triangles)
    const float wallVerts[] = {
        -0.5f, -0.7f, 0.25f, 0.25f, 0.28f, //
        0.5f,  -0.7f, 0.25f, 0.25f, 0.28f, //
        0.5f,  0.7f,  0.25f, 0.25f, 0.28f, //
        -0.5f, -0.7f, 0.25f, 0.25f, 0.28f, //
        0.5f,  0.7f,  0.25f, 0.25f, 0.28f, //
        -0.5f, 0.7f,  0.25f, 0.25f, 0.28f, //
    };
    gl::Buffer wallVbo;
    wallVbo.Allocate(gl::BufferType::ARRAY, wallVerts, sizeof(wallVerts),
                     gl::UsageType::STATIC_DRAW);

    const gl::VertexAttrib layout[] = {
        {gl::VertexAttribType::FLOAT, 2, 0, false}, // position
        {gl::VertexAttribType::FLOAT, 3, 0, false}, // color
    };
    gl::VertexArray triVao;
    triVao.AddVertexBuffer(triVbo, layout, 2, 5 * sizeof(float));
    gl::VertexArray wallVao;
    wallVao.AddVertexBuffer(wallVbo, layout, 2, 5 * sizeof(float));

    // ping-pong queries: write into one, read the other (previous frame)
    gl::Query queries[2];
    queries[0].Create(gl::QueryType::SAMPLES_PASSED);
    queries[1].Create(gl::QueryType::SAMPLES_PASSED);

    gl::Renderer::SetDepthTest(true);
    gl::Renderer::ClearColor(0.08f, 0.08f, 0.12f, 1.0f);

    int frame = 0;
    gl::u64 visibleSamples = 0;
    int visibleFrames = 0, hiddenFrames = 0;

    while (app.PollEvents())
    {
        app.BeginFrame();
        gl::Renderer::Clear(true, true);

        shader.Bind();

        // wall slides horizontally in front of the triangle (closer to camera)
        float wallX = 1.6f * sinf((float)frame / 90.0f);
        shader.SetVec2("u_pos", wallX, 0.0f);
        shader.SetFloat("u_depth", -0.5f);
        wallVao.Bind();
        gl::Renderer::Draw(gl::RenderPrimitive::TRIANGLES, 6);

        // triangle behind, wrapped in the occlusion query
        gl::Query& write = queries[frame & 1];
        gl::Query& read = queries[(frame + 1) & 1];

        shader.SetVec2("u_pos", 0.0f, 0.0f);
        shader.SetFloat("u_depth", 0.5f);
        triVao.Bind();
        write.Begin();
        gl::Renderer::Draw(gl::RenderPrimitive::TRIANGLES, 3);
        write.End();

        if (frame > 0)
        {
            visibleSamples = read.GetResult();
            if (visibleSamples > 0)
                ++visibleFrames;
            else
                ++hiddenFrames;
        }

        if (frame % 60 == 0)
            printf("frame %4d | wall x %+.2f | triangle samples: %llu%s\n", frame, wallX,
                   (unsigned long long)visibleSamples, visibleSamples == 0 ? "  (occluded)" : "");

        app.EndFrame();
        ++frame;
        if (maxFrames > 0 && frame >= maxFrames) break;
    }

    printf("frames: %d | visible: %d | occluded: %d | draw calls: %llu\n", frame, visibleFrames,
           hiddenFrames, (unsigned long long)gl::Renderer::GetStats().drawCalls);

    app.Destroy();
    return 0;
}
