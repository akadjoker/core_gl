#pragma once

#include "gl_config.hpp"

namespace gl
{

enum class TextureFormat : u8
{
    R8,
    R16F,
    R32F,
    RG8,
    RG16F,
    RG32F,
    RGB8,
    RGB16F,
    RGB32F,
    RGBA8,
    RGBA16F,
    RGBA32F,
    SRGB8,
    SRGB8_ALPHA8,
    DEPTH16,
    DEPTH24,
    DEPTH32F,
    DEPTH24_STENCIL8
};

enum class TextureWrap : u8
{
    REPEAT,
    MIRRORED_REPEAT,
    CLAMP_TO_EDGE,
    CLAMP_TO_BORDER
};

enum class TextureFilter : u8
{
    NEAREST,
    LINEAR,
    NEAREST_MIPMAP_NEAREST,
    LINEAR_MIPMAP_NEAREST,
    NEAREST_MIPMAP_LINEAR,
    LINEAR_MIPMAP_LINEAR
};

enum class PipelineStage : u8
{
    VERTEX = 0,
    GEOMETRY,
    FRAGMENT,
    COMPUTE,
    STAGE_COUNT
};

enum class BlendFactor : u8
{
    ZERO,
    ONE,
    SRC_COLOR,
    ONE_MINUS_SRC_COLOR,
    DST_COLOR,
    ONE_MINUS_DST_COLOR,
    SRC_ALPHA,
    ONE_MINUS_SRC_ALPHA,
    DST_ALPHA,
    ONE_MINUS_DST_ALPHA
};

enum class BlendOp : u8
{
    ADD,
    SUBTRACT,
    REVERSE_SUBTRACT,
    MIN,
    MAX
};

enum class DepthFunction : u8
{
    NEVER,
    LESS,
    EQUAL,
    LEQUAL,
    GREATER,
    NOT_EQUAL,
    GEQUAL,
    ALWAYS
};

enum class CullMode : u8
{
    NONE,
    FRONT,
    BACK,
    FRONT_AND_BACK
};

enum class StencilOp : u8
{
    KEEP,
    ZERO,
    REPLACE,
    INCR,
    INCR_WRAP,
    DECR,
    DECR_WRAP,
    INVERT
};

enum class RenderPrimitive : u8
{
    POINTS,
    LINES,
    LINE_STRIP,
    LINE_LOOP,
    TRIANGLES,
    TRIANGLE_STRIP,
    TRIANGLE_FAN
};

enum class Attachment : u8
{
    COLOR0,
    COLOR1,
    COLOR2,
    COLOR3,
    COLOR4,
    COLOR5,
    COLOR6,
    COLOR7,
    COLOR8,
    COLOR9,
    COLOR10,
    COLOR11,
    COLOR12,
    COLOR13,
    COLOR14,
    COLOR15,
    DEPTH,
    STENCIL,
    DEPTH_STENCIL
};

enum class UsageType : u8
{
    STREAM_DRAW,
    STREAM_READ,
    STREAM_COPY,
    STATIC_DRAW,
    STATIC_READ,
    STATIC_COPY,
    DYNAMIC_DRAW,
    DYNAMIC_READ,
    DYNAMIC_COPY
};

enum class BufferType : u8
{
    UNKNOWN,
    ARRAY,
    ELEMENT_ARRAY,
    SHADER_STORAGE,
    UNIFORM
};

enum class VertexAttribType : u8
{
    BYTE,
    UBYTE,
    SHORT,
    USHORT,
    INT,
    UINT,
    FLOAT,
    HALF_FLOAT
};

enum class QueryType : u8
{
    SAMPLES_PASSED,
    ANY_SAMPLES_PASSED,
    PRIMITIVES_GENERATED,
    TIME_ELAPSED
};

struct VertexAttrib
{
    VertexAttribType type;
    u8 components; // 1..4
    u8 divisor;    // 0 = per vertex, 1 = per instance
    bool normalized;
};

struct RenderStats
{
    u64 drawCalls = 0;
    u64 triangles = 0; // primitives actually submitted (instances included)
    u64 lines = 0;
    u64 points = 0;
    u64 shaderSwitches = 0;
    u64 textureBinds = 0;
    u64 bufferBinds = 0;
    u64 vaoSwitches = 0;
    u64 fboSwitches = 0;
    u64 stateChanges = 0; // enable/disable, blend, depth, cull...
    void Reset() { *this = RenderStats(); }
};

} // namespace gl
