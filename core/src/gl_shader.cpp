#include "coregl/gl_shader.hpp"
#include "gl_platform.hpp"
#include "gl_state.hpp"
#include <cstring>
#include <cstdio>

namespace gl
{

static const GLenum kStage[] = {
    GL_VERTEX_SHADER,
#if defined(CORE_GL_ES)
    0, // geometry shaders: not available on ES 3.1
#else
    GL_GEOMETRY_SHADER,
#endif
    GL_FRAGMENT_SHADER,
#if defined(__APPLE__)
    0, // compute: macOS caps at GL 4.1
#else
    GL_COMPUTE_SHADER,
#endif
};

// FNV-1a
static u32 hashName(const char* name)
{
    u32 h = 2166136261u;
    while (*name)
    {
        h ^= (u8)*name++;
        h *= 16777619u;
    }
    return h;
}

Shader::Shader() = default;

void Shader::Release()
{
    if (state::ContextAlive())
    {
        for (u32 stage : stages)
            if (stage) glDeleteShader(stage);
        if (id) glDeleteProgram(id);
    }
    for (u32& stage : stages)
        stage = 0;
    id = 0;
    uniformCache.clear();
    log[0] = 0;
}

Shader::~Shader()
{
    Release();
}

Shader::Shader(Shader&& other) noexcept
    : id(other.id), uniformCache(static_cast<std::unordered_map<u32, i32>&&>(other.uniformCache))
{
    for (int i = 0; i < (int)PipelineStage::STAGE_COUNT; ++i)
    {
        stages[i] = other.stages[i];
        other.stages[i] = 0;
    }
    memcpy(log, other.log, sizeof(log));
    other.id = 0;
}

Shader& Shader::operator=(Shader&& other) noexcept
{
    if (this != &other)
    {
        this->~Shader();
        new (this) Shader(static_cast<Shader&&>(other));
    }
    return *this;
}

bool Shader::LoadFromString(PipelineStage stage, const char* source)
{
    log[0] = 0;
    GLenum glStage = kStage[(u8)stage];
    if (glStage == 0)
    {
        snprintf(log, sizeof(log), "shader stage not supported on this platform");
        return false;
    }

    GLuint shader = glCreateShader(glStage);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);

    GLint ok = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok)
    {
        glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
        glDeleteShader(shader);
        return false;
    }

    u32& slot = stages[(u8)stage];
    if (slot) glDeleteShader(slot);
    slot = shader;
    return true;
}

bool Shader::Link()
{
    log[0] = 0;

    if (id)
    {
        glDeleteProgram(id);
        id = 0;
        uniformCache.clear();
    }

    GLuint program = glCreateProgram();
    for (u32 stage : stages)
        if (stage) glAttachShader(program, stage);

    glLinkProgram(program);

    for (u32& stage : stages)
    {
        if (stage)
        {
            glDetachShader(program, stage);
            glDeleteShader(stage);
            stage = 0;
        }
    }

    GLint ok = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &ok);
    if (!ok)
    {
        glGetProgramInfoLog(program, sizeof(log), nullptr, log);
        glDeleteProgram(program);
        return false;
    }

    id = program;

    // Introspect all active uniforms once: after this, GetLocation never
    // touches the GL driver again.
    GLint count = 0;
    glGetProgramiv(program, GL_ACTIVE_UNIFORMS, &count);
    uniformCache.reserve((size_t)count);
    char name[256];
    for (GLint i = 0; i < count; ++i)
    {
        GLint size = 0;
        GLenum type = 0;
        glGetActiveUniform(program, (GLuint)i, sizeof(name), nullptr, &size, &type, name);

        // arrays are reported as "name[0]" — also cache the bare name
        i32 location = glGetUniformLocation(program, name);
        uniformCache[hashName(name)] = location;
        char* bracket = strchr(name, '[');
        if (bracket)
        {
            *bracket = 0;
            uniformCache[hashName(name)] = location;
        }
    }
    return true;
}

void Shader::Bind()
{
    state::BindProgram(id);
}

void Shader::Unbind()
{
    state::BindProgram(0);
}

i32 Shader::GetLocation(const char* name) const
{
    // pure map lookup — the cache was fully populated at Link()
    auto it = uniformCache.find(hashName(name));
    return (it != uniformCache.end()) ? it->second : -1;
}

i32 Shader::GetAttribLocation(const char* name) const
{
    return id ? glGetAttribLocation(id, name) : -1;
}

void Shader::SetInt(const char* name, i32 v)
{
    Bind();
    glUniform1i(GetLocation(name), v);
}

void Shader::SetFloat(const char* name, f32 v)
{
    Bind();
    glUniform1f(GetLocation(name), v);
}

void Shader::SetVec2(const char* name, f32 x, f32 y)
{
    Bind();
    glUniform2f(GetLocation(name), x, y);
}

void Shader::SetVec3(const char* name, f32 x, f32 y, f32 z)
{
    Bind();
    glUniform3f(GetLocation(name), x, y, z);
}

void Shader::SetVec4(const char* name, f32 x, f32 y, f32 z, f32 w)
{
    Bind();
    glUniform4f(GetLocation(name), x, y, z, w);
}

void Shader::SetMat3(const char* name, const f32* m)
{
    Bind();
    glUniformMatrix3fv(GetLocation(name), 1, GL_FALSE, m);
}

void Shader::SetMat4(const char* name, const f32* m)
{
    Bind();
    glUniformMatrix4fv(GetLocation(name), 1, GL_FALSE, m);
}

// Hot path: by location, no hashing at all

void Shader::SetInt(i32 location, i32 v)
{
    Bind();
    glUniform1i(location, v);
}

void Shader::SetFloat(i32 location, f32 v)
{
    Bind();
    glUniform1f(location, v);
}

void Shader::SetVec2(i32 location, f32 x, f32 y)
{
    Bind();
    glUniform2f(location, x, y);
}

void Shader::SetVec3(i32 location, f32 x, f32 y, f32 z)
{
    Bind();
    glUniform3f(location, x, y, z);
}

void Shader::SetVec4(i32 location, f32 x, f32 y, f32 z, f32 w)
{
    Bind();
    glUniform4f(location, x, y, z, w);
}

void Shader::SetMat3(i32 location, const f32* m)
{
    Bind();
    glUniformMatrix3fv(location, 1, GL_FALSE, m);
}

void Shader::SetMat4(i32 location, const f32* m)
{
    Bind();
    glUniformMatrix4fv(location, 1, GL_FALSE, m);
}

void Shader::BindUniformBlock(const char* blockName, u32 bindingPoint)
{
    GLuint blockIndex = glGetUniformBlockIndex(id, blockName);
    if (blockIndex == GL_INVALID_INDEX) return; // block not active in this program
    glUniformBlockBinding(id, blockIndex, bindingPoint);
}

} // namespace gl
