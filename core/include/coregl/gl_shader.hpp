#pragma once

#include "gl_types.hpp"
#include <unordered_map>

namespace gl
{

class Shader
{
    u32 id = 0;                                      // GL program
    u32 stages[(u8)PipelineStage::STAGE_COUNT] = {}; // compiled shader per stage
    std::unordered_map<u32, i32> uniformCache;       // FNV-1a hash of name -> location
    char log[1024] = {};                             // last compile/link error

public:
    Shader();
    ~Shader();
    Shader(Shader&& other) noexcept;
    Shader& operator=(Shader&& other) noexcept;
    Shader(const Shader&) = delete;
    Shader& operator=(const Shader&) = delete;

    void Release();

    // Compiles one stage from GLSL source (must include #version).
    // Returns false on error — see GetLog().
    bool LoadFromString(PipelineStage stage, const char* source);

    // Links the loaded stages into a program and introspects every active
    // uniform (glGetActiveUniform), pre-filling the location cache — after
    // Link, uniform lookups never call the GL driver. Returns false on error.
    bool Link();

    void Bind();
    void Unbind();

    // Uniforms by name: one hash + map lookup, no GL call, no allocation
    void SetInt(const char* name, i32 v);
    void SetFloat(const char* name, f32 v);
    void SetVec2(const char* name, f32 x, f32 y);
    void SetVec3(const char* name, f32 x, f32 y, f32 z);
    void SetVec4(const char* name, f32 x, f32 y, f32 z, f32 w);
    void SetMat3(const char* name, const f32* m); // col-major, 9 floats
    void SetMat4(const char* name, const f32* m); // col-major, 16 floats

    // Hot path: fetch the location once (after Link) and set by location
    void SetInt(i32 location, i32 v);
    void SetFloat(i32 location, f32 v);
    void SetVec2(i32 location, f32 x, f32 y);
    void SetVec3(i32 location, f32 x, f32 y, f32 z);
    void SetVec4(i32 location, f32 x, f32 y, f32 z, f32 w);
    void SetMat3(i32 location, const f32* m);
    void SetMat4(i32 location, const f32* m);

    // -1 if the uniform is not active in the linked program
    i32 GetLocation(const char* name) const;
    i32 GetAttribLocation(const char* name) const;

    // Connects a "layout(std140) uniform <blockName> {...}" block declared in
    // this program to a uniform-buffer binding point (see Buffer::BindBase).
    // Any Buffer bound to that same point is read by every shader that
    // called this with the same blockName + bindingPoint — set once after
    // Link(), not per frame. No-op if the block isn't active in this program
    // (e.g. optimized out for not being read).
    void BindUniformBlock(const char* blockName, u32 bindingPoint);
    const char* GetLog() const { return log; }
    u32 GetHandle() const { return id; }
    bool IsValid() const { return id != 0; }
};

} // namespace gl
