#pragma once

// Compute smoke test: an SSBO of 1024 floats goes through a compute shader
// (data[i] = data[i] * 2 + i), then is read back and verified on the CPU.
// This is the exact path the particle system shaders depend on.

#include "test_common.hpp"
#include <cmath>

static const char* kComputeSrc = R"(#version 430 core
layout(local_size_x = 64) in;
layout(std430, binding = 0) buffer Data
{
    float data[];
};
uniform float u_mul;
void main()
{
    uint i = gl_GlobalInvocationID.x;
    data[i] = data[i] * u_mul + float(i);
}
)";

inline int test_compute(int /*maxFrames*/)
{
    TestApp app;
    if (!app.Create("coregl - compute", 320, 240)) return 1;

    if (!gl::Renderer::HasComputeSupport())
    {
        printf("SKIP: no compute support on this context\n");
        app.Destroy();
        return 0;
    }

    gl::Shader compute;
    if (!compute.LoadFromString(gl::PipelineStage::COMPUTE, kComputeSrc) || !compute.Link())
    {
        fprintf(stderr, "FAIL compute shader: %s\n", compute.GetLog());
        app.Destroy();
        return 1;
    }

    const int N = 1024;
    float input[N];
    for (int i = 0; i < N; ++i)
        input[i] = (float)i * 0.5f;

    gl::Buffer ssbo;
    ssbo.Allocate(gl::BufferType::SHADER_STORAGE, input, sizeof(input),
                  gl::UsageType::DYNAMIC_COPY);
    ssbo.BindBase(0);

    compute.Bind();
    compute.SetFloat("u_mul", 2.0f);
    gl::Renderer::Dispatch(N / 64);
    gl::Renderer::MemoryBarrierSSBO();

    float output[N];
    ssbo.Download(output, sizeof(output));

    int failed = 0;
    for (int i = 0; i < N; ++i)
    {
        float expected = input[i] * 2.0f + (float)i; // = 2*i
        if (fabsf(output[i] - expected) > 0.0001f)
        {
            if (failed < 5)
                fprintf(stderr, "mismatch at %d: got %f expected %f\n", i, output[i], expected);
            ++failed;
        }
    }

    printf("%d/%d values correct — %s\n", N - failed, N,
           failed == 0 ? "COMPUTE OK" : "COMPUTE FAILED");

    compute.Release();
    ssbo.Release();
    app.Destroy();
    return failed == 0 ? 0 : 1;
}
