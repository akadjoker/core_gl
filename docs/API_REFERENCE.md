# API Reference

## Overview

All public symbols are in the `gl` namespace. Include only one header:

```cpp
#include <coregl/gl_core.hpp>
```

This pulls in all sub-headers automatically.

## Lifetime Model

Every GL object follows the same pattern:

```
Constructor (id = 0)
  → Init() / Allocate() / Load*()   // creates the GL object
  → Use (Bind, Set*, Draw, etc.)
  → Release()                       // destroys the GL object
  → Destructor (safety net only)
```

The destructor checks `Renderer::ContextAlive()` before calling `glDelete*` to
avoid undefined behavior after the GL context is destroyed. Always call `Release()`
explicitly when you're done with an object; the destructor is a fallback.

Copy is deleted on all objects. Move constructors/assignment operators are provided.

---

## Renderer

### Initialization

```cpp
static bool Renderer::Init(LoadProc proc);     // returns false if no GL context
static void Renderer::Shutdown();
static const char* Renderer::GetVersionString();
static const char* Renderer::GetRendererString();
static bool Renderer::HasComputeSupport();
```

Must be the first call, with a current GL context. The `LoadProc` is typically:
- `SDL_GL_GetProcAddress`
- `glfwGetProcAddress`
- `eglGetProcAddress`

### Viewport & Clear

```cpp
static void Renderer::Viewport(int x, int y, int w, int h);
static void Renderer::ClearColor(f32 r, f32 g, f32 b, f32 a);
static void Renderer::ClearDepth(f32 depth);
static void Renderer::Clear(bool color, bool depth, bool stencil = false);
```

### Depth

```cpp
static void Renderer::SetDepthTest(bool enable);
static void Renderer::SetDepthWrite(bool enable);
static void Renderer::SetDepthFunction(DepthFunction func);
```

### Blend

```cpp
static void Renderer::SetBlend(bool enable);
static void Renderer::SetBlendFactors(BlendFactor src, BlendFactor dst);
static void Renderer::SetBlendFactorsSeparate(BlendFactor srcRGB, BlendFactor dstRGB,
                                              BlendFactor srcA, BlendFactor dstA);
static void Renderer::SetBlendOp(BlendOp op);
```

### Culling

```cpp
static void Renderer::SetCull(CullMode mode);
static void Renderer::SetFrontFaceCW(bool cw);
```

### Stencil

```cpp
static void Renderer::SetStencilTest(bool enable);
static void Renderer::SetStencilFunction(DepthFunction func, i32 ref, u32 mask = 0xFF);
static void Renderer::SetStencilOp(StencilOp stencilFail, StencilOp depthFail,
                                   StencilOp depthPass);
static void Renderer::SetStencilWriteMask(u32 mask);
```

### Polygon Offset

```cpp
static void Renderer::SetPolygonOffset(bool enable, f32 factor = 0.f, f32 units = 0.f);
```

### Color Mask & Scissor

```cpp
static void Renderer::SetColorWrite(bool r, bool g, bool b, bool a);
static void Renderer::SetScissor(bool enable);
static void Renderer::SetScissorRect(int x, int y, int w, int h);
```

### Drawing

```cpp
static void Renderer::Draw(RenderPrimitive prim, u32 vertexCount, u32 firstVertex = 0);
static void Renderer::DrawIndexed(RenderPrimitive prim, u32 indexCount, u32 firstIndex = 0);
static void Renderer::DrawInstanced(RenderPrimitive prim, u32 vertexCount, u32 instanceCount,
                                    u32 firstVertex = 0);
static void Renderer::DrawIndexedInstanced(RenderPrimitive prim, u32 indexCount, u32 instanceCount,
                                           u32 firstIndex = 0);
```

### Compute (Desktop GL 4.3+ / ES 3.1+)

```cpp
static void Renderer::Dispatch(u32 groupsX, u32 groupsY = 1, u32 groupsZ = 1);
static void Renderer::MemoryBarrierSSBO();
static void Renderer::MemoryBarrierAll();
```

### Other

```cpp
static void Renderer::ReadPixels(int x, int y, int w, int h, void* out);
static void Renderer::BindScreen();
static const RenderStats& Renderer::GetStats();
static void Renderer::ResetStats();
```

---

## Shader

```cpp
class Shader {
public:
    Shader();
    ~Shader();
    Shader(Shader&& other) noexcept;
    Shader& operator=(Shader&& other) noexcept;
    Shader(const Shader&) = delete;
    Shader& operator=(const Shader&) = delete;

    void Release();

    // Compiles one stage from GLSL source (must include #version)
    bool LoadFromString(PipelineStage stage, const char* source);

    // Links and introspects uniforms
    bool Link();

    void Bind();
    void Unbind();

    // Uniform setters by name (hash lookup, no GL call)
    void SetInt(const char* name, i32 v);
    void SetFloat(const char* name, f32 v);
    void SetVec2(const char* name, f32 x, f32 y);
    void SetVec3(const char* name, f32 x, f32 y, f32 z);
    void SetVec4(const char* name, f32 x, f32 y, f32 z, f32 w);
    void SetMat3(const char* name, const f32* m);  // col-major, 9 floats
    void SetMat4(const char* name, const f32* m);  // col-major, 16 floats

    // Hot path: by location (no hashing)
    void SetInt(i32 location, i32 v);
    void SetFloat(i32 location, f32 v);
    void SetVec2(i32 location, f32 x, f32 y);
    void SetVec3(i32 location, f32 x, f32 y, f32 z);
    void SetVec4(i32 location, f32 x, f32 y, f32 z, f32 w);
    void SetMat3(i32 location, const f32* m);
    void SetMat4(i32 location, const f32* m);

    i32 GetLocation(const char* name) const;
    i32 GetAttribLocation(const char* name) const;
    const char* GetLog() const;
    u32 GetHandle() const;
    bool IsValid() const;
};
```

---

## Buffer

```cpp
class Buffer {
public:
    Buffer();
    ~Buffer();
    Buffer(Buffer&& other) noexcept;
    Buffer& operator=(Buffer&& other) noexcept;
    Buffer(const Buffer&) = delete;

    void Release();

    // Allocates and optionally initializes
    void Allocate(BufferType type, const void* data, size_t byteSize, UsageType usage);

    // Partial update
    void Upload(const void* data, size_t byteSize, size_t offset = 0);

    // Readback
    void Download(void* out, size_t byteSize, size_t offset = 0);

    void Bind();
    void Unbind();
    void BindBase(u32 index);  // for SSBO / UBO

    size_t GetSize() const;
    u32 GetHandle() const;
    BufferType GetType() const;
    bool IsValid() const;
};
```

### Buffer Types

| Value | GL Target |
|-------|-----------|
| `UNKNOWN` | — |
| `ARRAY` | `GL_ARRAY_BUFFER` |
| `ELEMENT_ARRAY` | `GL_ELEMENT_ARRAY_BUFFER` |
| `SHADER_STORAGE` | `GL_SHADER_STORAGE_BUFFER` |
| `UNIFORM` | `GL_UNIFORM_BUFFER` |

### Usage Types

`STREAM_DRAW`, `STREAM_READ`, `STREAM_COPY`, `STATIC_DRAW`, `STATIC_READ`,
`STATIC_COPY`, `DYNAMIC_DRAW`, `DYNAMIC_READ`, `DYNAMIC_COPY`

---

## VertexArray

```cpp
class VertexArray {
public:
    VertexArray();
    ~VertexArray();
    VertexArray(VertexArray&& other) noexcept;
    VertexArray& operator=(VertexArray&& other) noexcept;
    VertexArray(const VertexArray&) = delete;

    void Release();

    void Bind();
    void Unbind();

    // Vertex buffer with custom layout
    void AddVertexBuffer(const Buffer& vbo, const VertexAttrib* attribs,
                         u32 attribCount, u32 stride);

    // Instance buffer (divisor = 1 on all attributes)
    void AddInstanceBuffer(const Buffer& vbo, const VertexAttrib* attribs,
                           u32 attribCount, u32 stride);

    void SetIndexBuffer(const Buffer& ibo, VertexAttribType indexType = VertexAttribType::UINT);

    VertexAttribType GetIndexType() const;
    u32 GetHandle() const;
    bool IsValid() const;
};
```

### VertexAttrib

```cpp
struct VertexAttrib {
    VertexAttribType type;  // BYTE, UBYTE, SHORT, USHORT, INT, UINT, FLOAT, HALF_FLOAT
    u8 components;          // 1–4
    u8 divisor;             // 0 = per vertex, 1 = per instance
    bool normalized;
};
```

---

## Texture

```cpp
class Texture {
public:
    Texture();
    ~Texture();
    Texture(Texture&& other) noexcept;
    Texture& operator=(Texture&& other) noexcept;
    Texture(const Texture&) = delete;

    void Release();

    void Load2D(const void* data, int w, int h, TextureFormat format);
    void LoadCube(const void* data[6], int size, TextureFormat format);
    void LoadDepth(int w, int h, TextureFormat format = TextureFormat::DEPTH32F);
    void LoadDepthCube(int size, TextureFormat format = TextureFormat::DEPTH32F);

    void Bind(u32 unit);
    void Unbind(u32 unit);

    void SetWrap(TextureWrap s, TextureWrap t, TextureWrap r = TextureWrap::REPEAT);
    void SetFilter(TextureFilter minFilter, TextureFilter magFilter);
    void GenerateMipmaps();

    int GetWidth() const;
    int GetHeight() const;
    TextureFormat GetFormat() const;
    u32 GetHandle() const;
    bool IsValid() const;
    bool IsCube() const;
};
```

### Texture Formats

| Format | Internal | Format | Type |
|--------|----------|--------|------|
| `R8` | `GL_R8` | `GL_RED` | `GL_UNSIGNED_BYTE` |
| `R16F` | `GL_R16F` | `GL_RED` | `GL_HALF_FLOAT` |
| `R32F` | `GL_R32F` | `GL_RED` | `GL_FLOAT` |
| `RG8` | `GL_RG8` | `GL_RG` | `GL_UNSIGNED_BYTE` |
| `RGBA8` | `GL_RGBA8` | `GL_RGBA` | `GL_UNSIGNED_BYTE` |
| `SRGB8` | `GL_SRGB8` | `GL_RGB` | `GL_UNSIGNED_BYTE` |
| `SRGB8_ALPHA8` | `GL_SRGB8_ALPHA8` | `GL_RGBA` | `GL_UNSIGNED_BYTE` |
| `DEPTH16` | `GL_DEPTH_COMPONENT16` | `GL_DEPTH_COMPONENT` | `GL_UNSIGNED_SHORT` |
| `DEPTH24` | `GL_DEPTH_COMPONENT24` | `GL_DEPTH_COMPONENT` | `GL_UNSIGNED_INT` |
| `DEPTH32F` | `GL_DEPTH_COMPONENT32F` | `GL_DEPTH_COMPONENT` | `GL_FLOAT` |
| `DEPTH24_STENCIL8` | `GL_DEPTH24_STENCIL8` | `GL_DEPTH_STENCIL` | `GL_UNSIGNED_INT_24_8` |

---

## FrameBuffer

```cpp
class FrameBuffer {
public:
    FrameBuffer();
    ~FrameBuffer();
    FrameBuffer(FrameBuffer&& other) noexcept;
    FrameBuffer& operator=(FrameBuffer&& other) noexcept;
    FrameBuffer(const FrameBuffer&) = delete;

    void Release();

    void Bind();
    void Unbind();

    void AttachTexture(const Texture& tex, Attachment attachment, u32 mipLevel = 0);
    void AttachCubeFace(const Texture& tex, Attachment attachment, u32 face, u32 mipLevel = 0);
    void Detach(Attachment attachment);
    void SetDrawBuffers();  // call after attaching color textures
    bool IsComplete();

    u32 GetHandle() const;
    bool IsValid() const;
};
```

Attachments: `COLOR0`–`COLOR15`, `DEPTH`, `STENCIL`, `DEPTH_STENCIL`

---

## Query

```cpp
class Query {
public:
    Query();
    ~Query();
    Query(Query&& other) noexcept;
    Query& operator=(Query&& other) noexcept;
    Query(const Query&) = delete;

    void Create(QueryType type);
    void Release();
    void Begin();
    void End();
    bool IsReady() const;
    u64 GetResult() const;

    u32 GetHandle() const;
    bool IsValid() const;
};
```

Query types: `SAMPLES_PASSED`, `ANY_SAMPLES_PASSED`, `PRIMITIVES_GENERATED`, `TIME_ELAPSED`

---

## Batch (Immediate-Mode Batcher)

```cpp
class Batch {
public:
    Batch();
    ~Batch();
    Batch(const Batch&) = delete;
    Batch& operator=(const Batch&) = delete;

    bool Init(u32 maxVertices = 8192);
    void Release();

    void SetProjection(const f32* mat4);

    // Transform stack
    void PushMatrix();
    void PopMatrix();
    void LoadIdentity();
    void Translate(f32 x, f32 y, f32 z = 0.f);
    void Rotate(f32 angleDeg, f32 ax, f32 ay, f32 az);
    void Scale(f32 x, f32 y, f32 z = 1.f);
    void SetTransform(const f32* mat4);

    // State
    void SetMode(RenderPrimitive prim);
    void SetTexture(const Texture* tex);
    void SetColor(u8 r, u8 g, u8 b, u8 a = 255);
    void SetColorF(f32 r, f32 g, f32 b, f32 a = 1.0f);
    void TexCoord(f32 u, f32 v);

    // Vertices
    void Vertex2(f32 x, f32 y);
    void Vertex3(f32 x, f32 y, f32 z);

    // 2D shapes
    void Line(f32 x0, f32 y0, f32 x1, f32 y1);
    void Rect(f32 x, f32 y, f32 w, f32 h, bool fill = true);
    void Circle(f32 cx, f32 cy, f32 radius, bool fill = true, int segments = 32);
    void Triangle(f32 x1, f32 y1, f32 x2, f32 y2, f32 x3, f32 y3, bool fill = true);
    void Ellipse(f32 cx, f32 cy, f32 rx, f32 ry, bool fill = true, int segments = 32);
    void Ring(f32 cx, f32 cy, f32 rInner, f32 rOuter, bool fill = true, int segments = 32);
    void Arc(f32 cx, f32 cy, f32 radius, f32 startDeg, f32 endDeg, int segments = 16);
    void ThickLine(f32 x0, f32 y0, f32 x1, f32 y1, f32 thickness);
    void Polygon(f32 cx, f32 cy, int sides, f32 radius, f32 rotationDeg, bool fill = true);
    void Polyline(const f32* xyPairs, int pointCount);
    void Grid(f32 x, f32 y, f32 w, f32 h, f32 cellW, f32 cellH);

    // 3D wireframe
    void CubeWire(f32 cx, f32 cy, f32 cz, f32 sx, f32 sy, f32 sz);
    void SphereWire(f32 cx, f32 cy, f32 cz, f32 radius, int rings = 6, int slices = 16);
    void CylinderWire(f32 cx, f32 cy, f32 cz, f32 radius, f32 height, int slices = 16);
    void CapsuleWire(f32 cx, f32 cy, f32 cz, f32 radius, f32 height, int slices = 16);
    void Grid3D(f32 size, f32 step);
    void Axes(f32 size);

    // 3D solid (flat color)
    void Cube(f32 cx, f32 cy, f32 cz, f32 sx, f32 sy, f32 sz);
    void Sphere(f32 cx, f32 cy, f32 cz, f32 radius, int rings = 12, int slices = 24);
    void Cylinder(f32 cx, f32 cy, f32 cz, f32 radius, f32 height, int slices = 24);
    void Capsule(f32 cx, f32 cy, f32 cz, f32 radius, f32 height, int rings = 6, int slices = 24);

    // Textures
    void Quad(const Texture& tex, f32 x, f32 y, f32 w, f32 h);
    void Quad(const Texture& tex, f32 srcX, f32 srcY, f32 srcW, f32 srcH,
              f32 x, f32 y, f32 w, f32 h);

    // Text (embedded 8x8 font, ASCII 32..127)
    void Text(f32 x, f32 y, f32 size, const char* text);
    f32 TextWidth(f32 size, const char* text) const;

    void Render();

    u32 GetVertexCount() const;
    u32 GetIndexCount() const;
};
```

---

## Enums

### TextureFormat

`R8`, `R16F`, `R32F`, `RG8`, `RG16F`, `RG32F`, `RGB8`, `RGB16F`, `RGB32F`,
`RGBA8`, `RGBA16F`, `RGBA32F`, `SRGB8`, `SRGB8_ALPHA8`, `DEPTH16`, `DEPTH24`,
`DEPTH32F`, `DEPTH24_STENCIL8`

### TextureWrap

`REPEAT`, `MIRRORED_REPEAT`, `CLAMP_TO_EDGE`, `CLAMP_TO_BORDER`

### TextureFilter

`NEAREST`, `LINEAR`, `NEAREST_MIPMAP_NEAREST`, `LINEAR_MIPMAP_NEAREST`,
`NEAREST_MIPMAP_LINEAR`, `LINEAR_MIPMAP_LINEAR`

### BlendFactor

`ZERO`, `ONE`, `SRC_COLOR`, `ONE_MINUS_SRC_COLOR`, `DST_COLOR`,
`ONE_MINUS_DST_COLOR`, `SRC_ALPHA`, `ONE_MINUS_SRC_ALPHA`, `DST_ALPHA`,
`ONE_MINUS_DST_ALPHA`

### DepthFunction

`NEVER`, `LESS`, `EQUAL`, `LEQUAL`, `GREATER`, `NOT_EQUAL`, `GEQUAL`, `ALWAYS`

### CullMode

`NONE`, `FRONT`, `BACK`, `FRONT_AND_BACK`

### RenderPrimitive

`POINTS`, `LINES`, `LINE_STRIP`, `LINE_LOOP`, `TRIANGLES`, `TRIANGLE_STRIP`, `TRIANGLE_FAN`

### PipelineStage

`VERTEX`, `GEOMETRY`, `FRAGMENT`, `COMPUTE`

### Attachment

`COLOR0`–`COLOR15`, `DEPTH`, `STENCIL`, `DEPTH_STENCIL`

### StencilOp

`KEEP`, `ZERO`, `REPLACE`, `INCR`, `INCR_WRAP`, `DECR`, `DECR_WRAP`, `INVERT`

---

## RenderStats

```cpp
struct RenderStats {
    u64 drawCalls;
    u64 triangles;
    u64 lines;
    u64 points;
    u64 shaderSwitches;
    u64 textureBinds;
    u64 bufferBinds;
    u64 vaoSwitches;
    u64 fboSwitches;
    u64 stateChanges;

    void Reset();
};
```
