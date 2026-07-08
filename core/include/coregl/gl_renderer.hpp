#pragma once

#include "gl_types.hpp"

namespace gl
{

// GPU query (occlusion, timer, primitives)
class Query
{
    u32 id = 0;
    QueryType type = QueryType::SAMPLES_PASSED;

public:
    Query();
    ~Query();
    Query(Query&& other) noexcept;
    Query& operator=(Query&& other) noexcept;
    Query(const Query&) = delete;
    Query& operator=(const Query&) = delete;

    void Create(QueryType type);
    void Release(); // frees the GL object now; destructor is a safety net
    void Begin();
    void End();
    bool IsReady() const;
    u64 GetResult() const; // blocks if not ready

    u32 GetHandle() const { return id; }
    bool IsValid() const { return id != 0; }
};

class Renderer
{
public:
    // Must be the first call, with a current GL context.
    // Returns false if no context is available.
    static bool Init(LoadProc proc);
    static void Shutdown();

    // Driver info (valid after Init)
    static const char* GetVersionString();
    static const char* GetRendererString();

    // Viewport
    static void Viewport(int x, int y, int w, int h);

    // Depth
    static void SetDepthTest(bool enable);
    static void SetDepthWrite(bool enable);
    static void SetDepthFunction(DepthFunction func);

    // Blend
    static void SetBlend(bool enable);
    static void SetBlendFactors(BlendFactor src, BlendFactor dst);
    static void SetBlendFactorsSeparate(BlendFactor srcRGB, BlendFactor dstRGB, BlendFactor srcA,
                                        BlendFactor dstA);
    static void SetBlendOp(BlendOp op);

    // Culling
    static void SetCull(CullMode mode);
    static void SetFrontFaceCW(bool cw);

    // Stencil (light volumes, masking)
    static void SetStencilTest(bool enable);
    static void SetStencilFunction(DepthFunction func, i32 ref, u32 mask = 0xFF);
    static void SetStencilOp(StencilOp stencilFail, StencilOp depthFail, StencilOp depthPass);
    static void SetStencilWriteMask(u32 mask);

    // Polygon offset (shadow map bias)
    static void SetPolygonOffset(bool enable, f32 factor = 0.f, f32 units = 0.f);

    // Color mask
    static void SetColorWrite(bool r, bool g, bool b, bool a);

    // Scissor
    static void SetScissor(bool enable);
    static void SetScissorRect(int x, int y, int w, int h);

    // Clear
    static void ClearColor(f32 r, f32 g, f32 b, f32 a);
    static void ClearDepth(f32 depth);
    static void Clear(bool color, bool depth, bool stencil = false);

    // Drawing (a shader and a vertex array must be bound)
    static void Draw(RenderPrimitive prim, u32 vertexCount, u32 firstVertex = 0);
    static void DrawIndexed(RenderPrimitive prim, u32 indexCount, u32 firstIndex = 0);
    static void DrawInstanced(RenderPrimitive prim, u32 vertexCount, u32 instanceCount,
                              u32 firstVertex = 0);
    static void DrawIndexedInstanced(RenderPrimitive prim, u32 indexCount, u32 instanceCount,
                                     u32 firstIndex = 0);

    // Compute (desktop GL 4.3+ / ES 3.1; no-op elsewhere)
    static bool HasComputeSupport();
    static void Dispatch(u32 groupsX, u32 groupsY = 1, u32 groupsZ = 1);
    static void MemoryBarrierSSBO();
    static void MemoryBarrierAll();

    // Reads RGBA8 pixels from the currently bound framebuffer (y up from
    // the bottom); out must hold w*h*4 bytes
    static void ReadPixels(int x, int y, int w, int h, void* out);

    // Default framebuffer (screen)
    static void BindScreen();

    // Stats
    static const RenderStats& GetStats();
    static void ResetStats();
};

} // namespace gl
