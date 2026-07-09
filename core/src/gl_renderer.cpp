#include "coregl/gl_renderer.hpp"
#include "coregl/gl_framebuffer.hpp"
#include "gl_platform.hpp"
#include "gl_state.hpp"
#include <cstddef>

namespace gl
{

// ---------------------------------------------------------------------------
// enum -> GL tables
// ---------------------------------------------------------------------------

static const GLenum kDepthFunc[] = {GL_NEVER,   GL_LESS,     GL_EQUAL,  GL_LEQUAL,
                                    GL_GREATER, GL_NOTEQUAL, GL_GEQUAL, GL_ALWAYS};

static const GLenum kBlendFactor[] = {GL_ZERO,      GL_ONE,
                                      GL_SRC_COLOR, GL_ONE_MINUS_SRC_COLOR,
                                      GL_DST_COLOR, GL_ONE_MINUS_DST_COLOR,
                                      GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA,
                                      GL_DST_ALPHA, GL_ONE_MINUS_DST_ALPHA};

static const GLenum kBlendOp[] = {GL_FUNC_ADD, GL_FUNC_SUBTRACT, GL_FUNC_REVERSE_SUBTRACT, GL_MIN,
                                  GL_MAX};

static const GLenum kCullMode[] = {0, GL_FRONT, GL_BACK, GL_FRONT_AND_BACK};

static const GLenum kStencilOp[] = {GL_KEEP,      GL_ZERO, GL_REPLACE,   GL_INCR,
                                    GL_INCR_WRAP, GL_DECR, GL_DECR_WRAP, GL_INVERT};

static const GLenum kPrimitive[] = {GL_POINTS,    GL_LINES,          GL_LINE_STRIP,  GL_LINE_LOOP,
                                    GL_TRIANGLES, GL_TRIANGLE_STRIP, GL_TRIANGLE_FAN};

static const GLenum kBufferTarget[] = {0, GL_ARRAY_BUFFER, GL_ELEMENT_ARRAY_BUFFER,
                                       GL_SHADER_STORAGE_BUFFER, GL_UNIFORM_BUFFER};

static const GLenum kQueryTarget[] = {
#if defined(CORE_GL_ES)
    0, // SAMPLES_PASSED: desktop only
    GL_ANY_SAMPLES_PASSED,
    0, // PRIMITIVES_GENERATED: ES 3.2+
    0, // TIME_ELAPSED: desktop only
#else
    GL_SAMPLES_PASSED,
    GL_ANY_SAMPLES_PASSED,
    GL_PRIMITIVES_GENERATED,
    GL_TIME_ELAPSED,
#endif
};

// ---------------------------------------------------------------------------
// Central state cache: every Set* compares against the cache and only calls GL on change.
// Initial values = GL defaults for a fresh context.
// ---------------------------------------------------------------------------

struct State
{
    bool initialized = false;

    int viewport[4] = {0, 0, 0, 0};

    bool depthTest = false;
    bool depthWrite = true;
    DepthFunction depthFunc = DepthFunction::LESS;

    bool blend = false;
    BlendFactor srcRGB = BlendFactor::ONE, dstRGB = BlendFactor::ZERO;
    BlendFactor srcA = BlendFactor::ONE, dstA = BlendFactor::ZERO;
    BlendOp blendOp = BlendOp::ADD;

    CullMode cull = CullMode::NONE;
    bool frontFaceCW = false;

    bool stencilTest = false;
    DepthFunction stencilFunc = DepthFunction::ALWAYS;
    i32 stencilRef = 0;
    u32 stencilMask = 0xFF;
    StencilOp stencilFail = StencilOp::KEEP;
    StencilOp stencilDepthFail = StencilOp::KEEP;
    StencilOp stencilDepthPass = StencilOp::KEEP;
    u32 stencilWriteMask = 0xFFFFFFFFu;

    bool polygonOffset = false;
    f32 polygonOffsetFactor = 0.f;
    f32 polygonOffsetUnits = 0.f;

    bool clipDistance[8] = {};

    bool colorMask[4] = {true, true, true, true};

    bool scissor = false;
    int scissorRect[4] = {0, 0, 0, 0};

    f32 clearColor[4] = {0.f, 0.f, 0.f, 0.f};
    f32 clearDepth = 1.f;

    // bind caches
    u32 boundProgram = 0;
    u32 boundVAO = 0;
    u32 boundFBO = 0;
    u32 boundBuffer[5] = {0, 0, 0, 0, 0}; // indexed by BufferType
    u32 boundTexture[32] = {};
    GLenum indexType = GL_UNSIGNED_INT;
    u32 indexSize = 4;

    // capabilities
    int major = 0, minor = 0;
    bool hasCompute = false;

    // lazily created on first DrawFullscreenTriangle()/DrawQuad(); no
    // attributes are ever enabled on it — both draws derive their geometry
    // from gl_VertexID inside the vertex shader. Freed with the context.
    u32 proceduralVAO = 0;

    RenderStats stats;
};

static State s;

static void setCap(GLenum cap, bool enable)
{
    if (enable)
        glEnable(cap);
    else
        glDisable(cap);
    ++s.stats.stateChanges;
}

// ---------------------------------------------------------------------------
// state:: — internal cache access for the other modules (see gl_state.hpp)
// ---------------------------------------------------------------------------

namespace state
{

RenderStats& Stats()
{
    return s.stats;
}

bool ContextAlive()
{
    return s.initialized;
}

bool BindProgram(u32 id)
{
    if (id == s.boundProgram) return false;
    s.boundProgram = id;
    glUseProgram(id);
    ++s.stats.shaderSwitches;
    return true;
}

bool BindVAO(u32 id)
{
    if (id == s.boundVAO) return false;
    s.boundVAO = id;
    // element buffer binding is VAO-scoped state: our global cache is stale now
    s.boundBuffer[(u8)BufferType::ELEMENT_ARRAY] = (u32)-1;
    glBindVertexArray(id);
    ++s.stats.vaoSwitches;
    return true;
}

bool BindBuffer(BufferType type, u32 id)
{
    u32& cached = s.boundBuffer[(u8)type];
    if (id == cached) return false;
    cached = id;
    glBindBuffer(kBufferTarget[(u8)type], id);
    ++s.stats.bufferBinds;
    return true;
}

bool BindTexture(u32 unit, GLenum target, u32 id)
{
    if (unit >= 32) return false;
    if (id == s.boundTexture[unit]) return false;
    s.boundTexture[unit] = id;
    glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(target, id);
    ++s.stats.textureBinds;
    return true;
}

bool BindFBO(u32 id)
{
    if (id == s.boundFBO) return false;
    s.boundFBO = id;
    glBindFramebuffer(GL_FRAMEBUFFER, id);
    ++s.stats.fboSwitches;
    return true;
}

void SetIndexType(GLenum glType, u32 byteSize)
{
    s.indexType = glType;
    s.indexSize = byteSize;
}

GLenum IndexTypeGL()
{
    return s.indexType;
}

u32 IndexTypeSize()
{
    return s.indexSize;
}

GLenum BufferTarget(BufferType type)
{
    return kBufferTarget[(u8)type];
}

} // namespace state

// ---------------------------------------------------------------------------
// Renderer
// ---------------------------------------------------------------------------

bool Renderer::Init(LoadProc proc)
{
    // On Linux/ES/macOS prototypes come from the platform headers and proc is
    // not needed; it stays in the API for a future Windows target.
    (void)proc;

    // Validate that a GL context is current
    if (!glGetString(GL_VERSION)) return false;
    s = State();
    s.initialized = true;

    glGetIntegerv(GL_MAJOR_VERSION, &s.major);
    glGetIntegerv(GL_MINOR_VERSION, &s.minor);
#if defined(CORE_GL_ES)
    s.hasCompute = (s.major > 3) || (s.major == 3 && s.minor >= 1); // ES 3.1+
#elif defined(__APPLE__)
    s.hasCompute = false; // macOS caps at GL 4.1
#else
    s.hasCompute = (s.major > 4) || (s.major == 4 && s.minor >= 3); // GL 4.3+
#endif
    return true;
}

void Renderer::Shutdown()
{
    s = State();
}

const char* Renderer::GetVersionString()
{
    return s.initialized ? (const char*)glGetString(GL_VERSION) : "";
}

const char* Renderer::GetRendererString()
{
    return s.initialized ? (const char*)glGetString(GL_RENDERER) : "";
}

void Renderer::Viewport(int x, int y, int w, int h)
{
    if (x == s.viewport[0] && y == s.viewport[1] && w == s.viewport[2] && h == s.viewport[3])
        return;
    s.viewport[0] = x;
    s.viewport[1] = y;
    s.viewport[2] = w;
    s.viewport[3] = h;
    glViewport(x, y, w, h);
    ++s.stats.stateChanges;
}

void Renderer::SetDepthTest(bool enable)
{
    if (enable == s.depthTest) return;
    s.depthTest = enable;
    setCap(GL_DEPTH_TEST, enable);
}

void Renderer::SetDepthWrite(bool enable)
{
    if (enable == s.depthWrite) return;
    s.depthWrite = enable;
    glDepthMask(enable ? 1 : 0);
    ++s.stats.stateChanges;
}

void Renderer::SetDepthFunction(DepthFunction func)
{
    if (func == s.depthFunc) return;
    s.depthFunc = func;
    glDepthFunc(kDepthFunc[(u8)func]);
    ++s.stats.stateChanges;
}

void Renderer::SetBlend(bool enable)
{
    if (enable == s.blend) return;
    s.blend = enable;
    setCap(GL_BLEND, enable);
}

void Renderer::SetBlendFactors(BlendFactor src, BlendFactor dst)
{
    SetBlendFactorsSeparate(src, dst, src, dst);
}

void Renderer::SetBlendFactorsSeparate(BlendFactor srcRGB, BlendFactor dstRGB, BlendFactor srcA,
                                       BlendFactor dstA)
{
    if (srcRGB == s.srcRGB && dstRGB == s.dstRGB && srcA == s.srcA && dstA == s.dstA) return;
    s.srcRGB = srcRGB;
    s.dstRGB = dstRGB;
    s.srcA = srcA;
    s.dstA = dstA;
    glBlendFuncSeparate(kBlendFactor[(u8)srcRGB], kBlendFactor[(u8)dstRGB], kBlendFactor[(u8)srcA],
                        kBlendFactor[(u8)dstA]);
    ++s.stats.stateChanges;
}

void Renderer::SetBlendOp(BlendOp op)
{
    if (op == s.blendOp) return;
    s.blendOp = op;
    glBlendEquation(kBlendOp[(u8)op]);
    ++s.stats.stateChanges;
}

void Renderer::SetCull(CullMode mode)
{
    if (mode == s.cull) return;
    bool wasOff = (s.cull == CullMode::NONE);
    s.cull = mode;
    if (mode == CullMode::NONE)
    {
        setCap(GL_CULL_FACE, false);
        return;
    }
    if (wasOff) setCap(GL_CULL_FACE, true);
    glCullFace(kCullMode[(u8)mode]);
    ++s.stats.stateChanges;
}

void Renderer::SetFrontFaceCW(bool cw)
{
    if (cw == s.frontFaceCW) return;
    s.frontFaceCW = cw;
    glFrontFace(cw ? GL_CW : GL_CCW);
    ++s.stats.stateChanges;
}

void Renderer::SetStencilTest(bool enable)
{
    if (enable == s.stencilTest) return;
    s.stencilTest = enable;
    setCap(GL_STENCIL_TEST, enable);
}

void Renderer::SetStencilFunction(DepthFunction func, i32 ref, u32 mask)
{
    if (func == s.stencilFunc && ref == s.stencilRef && mask == s.stencilMask) return;
    s.stencilFunc = func;
    s.stencilRef = ref;
    s.stencilMask = mask;
    glStencilFunc(kDepthFunc[(u8)func], ref, mask);
    ++s.stats.stateChanges;
}

void Renderer::SetStencilOp(StencilOp stencilFail, StencilOp depthFail, StencilOp depthPass)
{
    if (stencilFail == s.stencilFail && depthFail == s.stencilDepthFail &&
        depthPass == s.stencilDepthPass)
        return;
    s.stencilFail = stencilFail;
    s.stencilDepthFail = depthFail;
    s.stencilDepthPass = depthPass;
    glStencilOp(kStencilOp[(u8)stencilFail], kStencilOp[(u8)depthFail], kStencilOp[(u8)depthPass]);
    ++s.stats.stateChanges;
}

void Renderer::SetStencilWriteMask(u32 mask)
{
    if (mask == s.stencilWriteMask) return;
    s.stencilWriteMask = mask;
    glStencilMask(mask);
    ++s.stats.stateChanges;
}

void Renderer::SetPolygonOffset(bool enable, f32 factor, f32 units)
{
    if (enable != s.polygonOffset)
    {
        s.polygonOffset = enable;
        setCap(GL_POLYGON_OFFSET_FILL, enable);
    }
    if (enable && (factor != s.polygonOffsetFactor || units != s.polygonOffsetUnits))
    {
        s.polygonOffsetFactor = factor;
        s.polygonOffsetUnits = units;
        glPolygonOffset(factor, units);
        ++s.stats.stateChanges;
    }
}

void Renderer::SetClipDistance(u32 index, bool enable)
{
#if defined(CORE_GL_ES)
    // no hardware clip planes on ES: clipping happens in-shader (CLIP_APPLY
    // discards), driven by the plane uniform alone
    (void)index;
    (void)enable;
#else
    if (index >= 8 || enable == s.clipDistance[index]) return;
    s.clipDistance[index] = enable;
    setCap(GL_CLIP_DISTANCE0 + index, enable);
#endif
}

const char* Renderer::ShaderHeader(PipelineStage stage)
{
#if defined(CORE_GL_ES)
    if (stage == PipelineStage::VERTEX)
        return "#version 300 es\n"
               "precision highp float;\n"
               "precision highp int;\n"
               "#define CLIP_VARYING out float v_clipDistance;\n"
               "#define CLIP_SETUP(d) v_clipDistance = (d)\n";
    return "#version 300 es\n"
           "precision highp float;\n"
           "precision highp int;\n"
           "#define CLIP_VARYING in float v_clipDistance;\n"
           "#define CLIP_APPLY if (v_clipDistance < 0.0) discard\n";
#else
    if (stage == PipelineStage::VERTEX)
        return "#version 430 core\n"
               "#define CLIP_VARYING\n"
               "#define CLIP_SETUP(d) gl_ClipDistance[0] = (d)\n";
    return "#version 430 core\n"
           "#define CLIP_VARYING\n"
           "#define CLIP_APPLY\n";
#endif
}

void Renderer::SetColorWrite(bool r, bool g, bool b, bool a)
{
    if (r == s.colorMask[0] && g == s.colorMask[1] && b == s.colorMask[2] && a == s.colorMask[3])
        return;
    s.colorMask[0] = r;
    s.colorMask[1] = g;
    s.colorMask[2] = b;
    s.colorMask[3] = a;
    glColorMask(r, g, b, a);
    ++s.stats.stateChanges;
}

void Renderer::SetScissor(bool enable)
{
    if (enable == s.scissor) return;
    s.scissor = enable;
    setCap(GL_SCISSOR_TEST, enable);
}

void Renderer::SetScissorRect(int x, int y, int w, int h)
{
    if (x == s.scissorRect[0] && y == s.scissorRect[1] && w == s.scissorRect[2] &&
        h == s.scissorRect[3])
        return;
    s.scissorRect[0] = x;
    s.scissorRect[1] = y;
    s.scissorRect[2] = w;
    s.scissorRect[3] = h;
    glScissor(x, y, w, h);
    ++s.stats.stateChanges;
}

void Renderer::ClearColor(f32 r, f32 g, f32 b, f32 a)
{
    if (r == s.clearColor[0] && g == s.clearColor[1] && b == s.clearColor[2] &&
        a == s.clearColor[3])
        return;
    s.clearColor[0] = r;
    s.clearColor[1] = g;
    s.clearColor[2] = b;
    s.clearColor[3] = a;
    glClearColor(r, g, b, a);
    ++s.stats.stateChanges;
}

void Renderer::ClearDepth(f32 depth)
{
    if (depth == s.clearDepth) return;
    s.clearDepth = depth;
#if defined(CORE_GL_ES)
    glClearDepthf(depth);
#else
    glClearDepth((GLdouble)depth);
#endif
    ++s.stats.stateChanges;
}

void Renderer::Clear(bool color, bool depth, bool stencil)
{
    GLbitfield mask = 0;
    if (color) mask |= GL_COLOR_BUFFER_BIT;
    if (depth) mask |= GL_DEPTH_BUFFER_BIT;
    if (stencil) mask |= GL_STENCIL_BUFFER_BIT;
    if (mask) glClear(mask);
}

// primitive count from element count, added to the stats
static void countPrims(RenderPrimitive prim, u32 count, u32 instances)
{
    u64 n = 0;
    switch (prim)
    {
        case RenderPrimitive::POINTS:
            n = count;
            break;
        case RenderPrimitive::LINES:
            n = count / 2;
            break;
        case RenderPrimitive::LINE_STRIP:
            n = count > 1 ? count - 1 : 0;
            break;
        case RenderPrimitive::LINE_LOOP:
            n = count;
            break;
        case RenderPrimitive::TRIANGLES:
            n = count / 3;
            break;
        case RenderPrimitive::TRIANGLE_STRIP:
        case RenderPrimitive::TRIANGLE_FAN:
            n = count > 2 ? count - 2 : 0;
            break;
    }
    n *= instances;
    if (prim == RenderPrimitive::POINTS)
        s.stats.points += n;
    else if ((u8)prim <= (u8)RenderPrimitive::LINE_LOOP)
        s.stats.lines += n;
    else
        s.stats.triangles += n;
}

void Renderer::Draw(RenderPrimitive prim, u32 vertexCount, u32 firstVertex)
{
    countPrims(prim, vertexCount, 1);
    glDrawArrays(kPrimitive[(u8)prim], (GLint)firstVertex, (GLsizei)vertexCount);
    ++s.stats.drawCalls;
}

void Renderer::DrawIndexed(RenderPrimitive prim, u32 indexCount, u32 firstIndex)
{
    countPrims(prim, indexCount, 1);
    const void* offset = (const void*)(size_t)(firstIndex * s.indexSize);
    glDrawElements(kPrimitive[(u8)prim], (GLsizei)indexCount, s.indexType, offset);
    ++s.stats.drawCalls;
}

void Renderer::DrawInstanced(RenderPrimitive prim, u32 vertexCount, u32 instanceCount,
                             u32 firstVertex)
{
    countPrims(prim, vertexCount, instanceCount);
    glDrawArraysInstanced(kPrimitive[(u8)prim], (GLint)firstVertex, (GLsizei)vertexCount,
                          (GLsizei)instanceCount);
    ++s.stats.drawCalls;
}

void Renderer::DrawIndexedInstanced(RenderPrimitive prim, u32 indexCount, u32 instanceCount,
                                    u32 firstIndex)
{
    countPrims(prim, indexCount, instanceCount);
    const void* offset = (const void*)(size_t)(firstIndex * s.indexSize);
    glDrawElementsInstanced(kPrimitive[(u8)prim], (GLsizei)indexCount, s.indexType, offset,
                            (GLsizei)instanceCount);
    ++s.stats.drawCalls;
}

static void ensureProceduralVAO()
{
    if (!s.proceduralVAO) glGenVertexArrays(1, &s.proceduralVAO);
    state::BindVAO(s.proceduralVAO);
}

void Renderer::DrawFullscreenTriangle()
{
    ensureProceduralVAO();
    glDrawArrays(GL_TRIANGLES, 0, 3);
    ++s.stats.drawCalls;
    countPrims(RenderPrimitive::TRIANGLES, 3, 1);
}

const char* Renderer::FullscreenTriangleShaderSource()
{
#if defined(CORE_GL_ES)
    return "#version 300 es\n"
#else
    return "#version 430 core\n"
#endif
           "out vec2 v_uv;\n"
           "void main()\n"
           "{\n"
           "    vec2 uv = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);\n"
           "    v_uv = uv;\n"
           "    gl_Position = vec4(uv * 2.0 - 1.0, 0.0, 1.0);\n"
           "}\n";
}

void Renderer::DrawQuad()
{
    ensureProceduralVAO();
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    ++s.stats.drawCalls;
    countPrims(RenderPrimitive::TRIANGLE_STRIP, 4, 1);
}

const char* Renderer::QuadShaderSource()
{
#if defined(CORE_GL_ES)
    return "#version 300 es\n"
#else
    return "#version 430 core\n"
#endif
           "uniform vec4 u_rect;       // x, y, w, h in target pixels, top-left origin\n"
           "uniform vec2 u_targetSize; // target width/height in pixels\n"
           "out vec2 v_uv;\n"
           "void main()\n"
           "{\n"
           "    vec2 corner = vec2(float(gl_VertexID & 1), float((gl_VertexID >> 1) & 1));\n"
           "    v_uv = corner;\n"
           "    vec2 pixelPos = u_rect.xy + corner * u_rect.zw;\n"
           "    vec2 ndc = pixelPos / u_targetSize * 2.0 - 1.0;\n"
           "    ndc.y = -ndc.y; // pixel y grows down, NDC y grows up\n"
           "    gl_Position = vec4(ndc, 0.0, 1.0);\n"
           "}\n";
}

bool Renderer::HasComputeSupport()
{
    return s.hasCompute;
}

void Renderer::Dispatch(u32 groupsX, u32 groupsY, u32 groupsZ)
{
#if defined(__APPLE__)
    (void)groupsX;
    (void)groupsY;
    (void)groupsZ;
#else
    if (!s.hasCompute) return;
    glDispatchCompute(groupsX, groupsY, groupsZ);
#endif
}

void Renderer::MemoryBarrierSSBO()
{
#if !defined(__APPLE__)
    if (!s.hasCompute) return;
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
#endif
}

void Renderer::MemoryBarrierAll()
{
#if !defined(__APPLE__)
    if (!s.hasCompute) return;
    glMemoryBarrier(GL_ALL_BARRIER_BITS);
#endif
}

void Renderer::ReadPixels(int x, int y, int w, int h, void* out)
{
    glReadPixels(x, y, w, h, GL_RGBA, GL_UNSIGNED_BYTE, out);
}

void Renderer::BlitFramebuffer(const FrameBuffer* src, const FrameBuffer* dst, int srcX0, int srcY0,
                               int srcX1, int srcY1, int dstX0, int dstY0, int dstX1, int dstY1,
                               bool color, bool depth, bool stencil, TextureFilter filter)
{
    glBindFramebuffer(GL_READ_FRAMEBUFFER, src ? src->GetHandle() : 0);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, dst ? dst->GetHandle() : 0);

    GLbitfield mask = 0;
    if (color) mask |= GL_COLOR_BUFFER_BIT;
    if (depth) mask |= GL_DEPTH_BUFFER_BIT;
    if (stencil) mask |= GL_STENCIL_BUFFER_BIT;

    // the GL spec requires NEAREST for any blit that includes depth/stencil
    GLenum glFilter =
        (depth || stencil || filter == TextureFilter::NEAREST) ? GL_NEAREST : GL_LINEAR;
    glBlitFramebuffer(srcX0, srcY0, srcX1, srcY1, dstX0, dstY0, dstX1, dstY1, mask, glFilter);

    // GL_READ_FRAMEBUFFER is still pointed at src — leaving it there would
    // make the next ReadPixels() (which reads GL_READ_FRAMEBUFFER, not the
    // combined GL_FRAMEBUFFER our cache tracks) silently read from the
    // wrong target. Point both read and draw back at dst, the natural
    // "keep working on what I just blitted into" state, and keep the cache
    // in sync so a later Bind()/BindScreen() behaves correctly.
    u32 dstId = dst ? dst->GetHandle() : 0;
    glBindFramebuffer(GL_FRAMEBUFFER, dstId);
    s.boundFBO = dstId;
    ++s.stats.fboSwitches;
}

void Renderer::BlitFramebuffer(const FrameBuffer& src, const FrameBuffer* dst, int w, int h,
                               bool color, bool depth, bool stencil)
{
    BlitFramebuffer(&src, dst, 0, 0, w, h, 0, 0, w, h, color, depth, stencil,
                    TextureFilter::NEAREST);
}

void Renderer::BindScreen()
{
    state::BindFBO(0);
}

// ---------------------------------------------------------------------------
// Query
// ---------------------------------------------------------------------------

Query::Query() = default;

void Query::Release()
{
    if (id && state::ContextAlive()) glDeleteQueries(1, &id);
    id = 0;
}

Query::~Query()
{
    Release();
}

Query::Query(Query&& other) noexcept : id(other.id), type(other.type)
{
    other.id = 0;
}

Query& Query::operator=(Query&& other) noexcept
{
    if (this != &other)
    {
        Release();
        id = other.id;
        type = other.type;
        other.id = 0;
    }
    return *this;
}

void Query::Create(QueryType queryType)
{
    Release();
    type = queryType;
    if (kQueryTarget[(u8)queryType] == 0) return; // not supported on this target
    glGenQueries(1, &id);
}

void Query::Begin()
{
    if (id) glBeginQuery(kQueryTarget[(u8)type], id);
}

void Query::End()
{
    if (id) glEndQuery(kQueryTarget[(u8)type]);
}

bool Query::IsReady() const
{
    if (!id) return false;
    GLuint ready = 0;
    glGetQueryObjectuiv(id, GL_QUERY_RESULT_AVAILABLE, &ready);
    return ready != 0;
}

u64 Query::GetResult() const
{
    if (!id) return 0;
#if defined(CORE_GL_ES)
    GLuint result = 0;
    glGetQueryObjectuiv(id, GL_QUERY_RESULT, &result);
    return (u64)result;
#else
    GLuint64 result = 0;
    glGetQueryObjectui64v(id, GL_QUERY_RESULT, &result);
    return (u64)result;
#endif
}

const RenderStats& Renderer::GetStats()
{
    return s.stats;
}
void Renderer::ResetStats()
{
    s.stats.Reset();
}

} // namespace gl
