# core_gl

Lightweight OpenGL/OpenGL ES abstraction layer -- zero external deps,
cross-platform (Desktop GL 4.3+ and OpenGL ES 3.1/WebGL2).

A fast, low-level OpenGL render backend in C++11. No dependencies, no exceptions,
no smart pointers -- plain C-style API over a cached GL state machine. This is
**not** an engine: it is the backend an engine is built on.

```cpp
#include <coregl/gl_core.hpp>

gl::Renderer::Init(SDL_GL_GetProcAddress);  // or glfwGetProcAddress, eglGetProcAddress...

gl::Batch batch;
batch.Init(65535);
batch.SetProjection(ortho);                 // raw float[16], bring your own math

batch.SetColor(255, 80, 80, 255);
batch.Rect(40, 40, 120, 80);
batch.Text(40, 140, 16, "hello from the embedded font");
batch.Render();
```

## Table of Contents

- **[Building](#building-coregl)** — Prerequisites, build commands, platform notes
- **[API Reference](#api-reference)** — Complete class/method/enum reference
- **[Architecture](#architecture)** — State cache, uniform cache, lifetime management
- **[Tests](#tests)** — Available tests and how to run them
- **[Batch Shapes](#batch-shapes)** — Catalog of 2D and 3D drawing primitives

## Platforms

| Target        | API           | Status                                  |
|---------------|---------------|-----------------------------------------|
| Linux desktop | OpenGL 4.3+   | working (primary target)                |
| Android       | OpenGL ES 3.1 | compiles (`-DCORE_GL_FORCE_ES`)         |
| Web           | WebGL2/ES 3.0 | compiles; no compute/geometry           |
| macOS         | OpenGL 4.1    | untested; no compute                    |
| Windows       | --             | not yet (needs a runtime loader)        |


## What's inside

- **Renderer** -- central GL state cache: every state change and bind is compared
  against the cache and only reaches the driver when it actually changes.
  Stats for everything: draw calls, triangles, lines, binds, state changes.
- **Shader** -- vertex/geometry/fragment/compute. All active uniforms are
  introspected once at `Link()`; uniform lookups never touch the driver again.
  Hot path setters by location, convenience setters by name (FNV-1a hash).
- **Buffer / VertexArray** -- VBO, IBO (u16/u32), SSBO, UBO, instancing.
- **Texture / FrameBuffer** -- 2D, cubemap, depth, depth cubemap; MRT up to 16
  color attachments, depth-only mode for shadow maps.
- **Query** -- hardware occlusion queries (samples passed), timers, primitives.
- **Batch** -- indexed immediate-mode renderer:
  - 24-byte vertex (pos3f + uv2f + packed RGBA8), u16 indices
  - quads are 4 verts + 6 indices, circle fills are indexed fans
  - VBO/IBO orphaned per flush -- no driver sync stalls
  - consecutive draws with the same texture+mode merge into one call
  - transform stack (`PushMatrix`/`Translate`/`Rotate`/`Scale`)
  - 2D: rect, circle, ellipse, ring, arc, polygon, polyline, thick line, grid
  - 3D solids: cube, sphere, cylinder, capsule (flat color)
  - 3D wireframes: all of the above + 3D grid + axes
  - text with an embedded public-domain 8x8 font 
- **Lifetime** -- explicit `Release()` on every GL object;
  destructors are a safety net that never touch a dead context.

## Numbers (AMD Renoir iGPU, Mesa)

- **50,000 textured quads**: 3.5 ms CPU submit -- **14.3 M quads/s**
- **Bunnymark**: 100,000 bunnies at **90 fps**; 353k at 32 fps (fill-bound)
- 120 frames of a spinning triangle: **1** shader switch, **1** VAO switch

# Building coregl

## Prerequisites

- **CMake 3.16+**
- **C++11 compiler** (GCC, Clang, MSVC)
- **SDL2** (optional — only required for tests)
- **OpenGL** (installed with your OS)

The library itself has **zero external dependencies**. SDL2 is only needed to run the test suite.

## Linux (Primary Target)

```bash
cmake -B build
cmake --build build -j
./build/tests/test_window triangle  # run a test
```

## macOS

macOS caps at OpenGL 4.1, so compute shaders and geometry shaders are disabled.

```bash
cmake -B build
cmake --build build -j
```

## Android / WebGL (ES Build)

Force OpenGL ES 3.1 compilation:

```bash
cmake -B build -DCORE_GL_FORCE_ES=ON
cmake --build build -j
```

## Building Without Tests

```bash
cmake -B build -DCOREGL_BUILD_TESTS=OFF
cmake --build build -j
```

## Installing

The library produces a static library (`libcoregl.a` on Linux) with headers under
`core/include/coregl/`. Install with:

```bash
cmake --install build
```

## Platform-Specific Notes

### Windows

Not yet supported. Requires a runtime GL loader (`wglGetProcAddress`).
Enable this by setting `CORE_GL_FORCE_DESKTOP` and implementing the Windows
loader path.

### iOS

Not yet supported. Would require Metal/Vulkan via MoltenVK.

## Test Targets

```bash
./build/tests/test_window clear
./build/tests/test_window triangle
./build/tests/test_window batch
./build/tests/test_window bench
./build/tests/test_window bunny
./build/tests/test_window shapes3d
./build/tests/test_window compute
./build/tests/test_window occlusion
./build/tests/test_window rendertarget
./build/tests/test_window verify
```

Run with a frame count to exit automatically (useful for CI):

```bash
./build/tests/test_window triangle 60   # renders 60 frames then exits
```

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

# Architecture

## Design Goals

- **Zero external dependencies** — the library has no dependencies whatsoever.
  The consumer provides the GL function loader (`LoadProc`).
- **Minimal C++** — C++11 with RAII + move semantics. No smart pointers, no
  exceptions, no RTTI, no `std::string`. The public API uses `const char*` and
  raw pointers.
- **Centralized state** — all GL state lives in one place (`Renderer::State`),
  reducing redundant driver calls.
- **Explicit lifetime** — `Release()` on every GL object; destructors are safety
  nets that guard against use-after-context-destruction.

## Core Abstractions

### 1. State Cache (`gl_state.hpp` / `gl_renderer.cpp`)

All GL state is tracked in a single `State` struct:

```cpp
struct State {
    u32 boundProgram, boundVAO, boundFBO;
    u32 boundBuffer[5];    // ARRAY, ELEMENT_ARRAY, SSBO, UNIFORM
    u32 boundTexture[32];  // one entry per texture unit
    bool depthTest, depthWrite, blend, scissor, stencilTest;
    DepthFunction depthFunc;
    BlendFactor srcRGB, dstRGB, srcA, dstA;
    BlendOp blendOp;
    CullMode cull;
    // ... plus clear color, color mask, scissor rect, polygon offset,
    //     stencil function/ops, front face, viewport, etc.
    RenderStats stats;
};
```

Every `Bind*()` / `Set*()` function compares the requested value against the
cached value. If they match, the GL call is skipped entirely. If they differ,
the GL call is made and `stats.stateChanges` is incremented.

The bind functions return `bool` indicating whether a real GL call happened
(cache miss = true, hit = false).

### 2. Uniform Cache (`gl_shader.cpp`)

Uniform lookups are optimized in two ways:

1. **At link time**: `Shader::Link()` introspects **all** active uniforms via
   `glGetActiveUniform` and stores `(hash(name), location)` in an
   `std::unordered_map`.
2. **At set time**: `SetInt("u_resolution", ...)` hashes the name, looks it up
   in the map (O(1)), and calls `glUniform1i(location, ...)`. Zero GL calls
   for the lookup.

For hot paths, the user can cache the location once:

```cpp
i32 loc = shader.GetLocation("u_time");
// per frame:
shader.SetFloat(loc, time);  // no hashing, no map lookup
```

Uniform arrays (e.g., `lights[0]`, `lights[1]`) are handled: both `"lights"`
and `"lights[0]"` return the same location.

### 3. Lifetime Management

Every GL object (Shader, Buffer, VAO, Texture, FrameBuffer, Query) follows:

```
Constructor → id = 0 (no GL call)
Init()/Allocate()/Load*() → glGen*()
Use (Bind, Set*, Draw, etc.)
Release() → glDelete*() (guarded by ContextAlive())
Destructor → safety net (only calls Release() if ContextAlive())
```

This ensures objects survive even if `Renderer::Shutdown()` is called before
they are released — the destructor won't call `glDelete*` on a dead context.

### 4. Platform Abstraction

- `gl_config.hpp`: detects platform (`CORE_GL_DESKTOP` vs `CORE_GL_ES`) and
  provides typedefs (`u8`, `u16`, `u32`, `f32`, `LoadProc`).
- `gl_platform.hpp`: includes the correct GL headers (`GL/glcorearb.h`,
  `<GLES3/gl31.h>`, `<OpenGL/gl3.h>`, or nothing for Windows stub).

### 5. Batch Batcher

The `Batch` class implements an indexed immediate-mode renderer:

- **Vertex format**: 24 bytes (`vec3 pos` + `vec2 uv` + `packed u32 color`)
- **Index format**: `u16` (batch capped at 65535 vertices per flush)
- **Orphaned buffers**: `glBufferData` with `nullptr` size each flush avoids
  driver sync stalls (the GPU may still be reading the old buffer contents).
- **Command merging**: consecutive draws with the same texture + primitive mode
  are merged into a single `glDrawElements` call.
- **Transform stack**: Push/Pop/LoadIdentity/Translate/Rotate/Scale — all
  composed on the CPU and applied to vertices at emit time.

### 6. Shader Pipeline

```
glCreateShader(glStage)
glShaderSource(shader, 1, &source, nullptr)
glCompileShader(shader)
glGetShaderiv(shader, GL_COMPILE_STATUS, &ok)

→ Attach to program
glLinkProgram(program)
glGetProgramiv(program, GL_LINK_STATUS, &ok)

→ Introspect uniforms
glGetProgramiv(program, GL_ACTIVE_UNIFORMS, &count)
for i in 0..count:
    glGetActiveUniform(program, i, ...)
    glGetUniformLocation(program, name)
    cache[hash(name)] = location
```

Stages are detached and deleted after linking to free memory.

## Stats Tracking

Every driver call increments counters in `RenderStats`:

| Counter | Incremented By |
|---------|---------------|
| `drawCalls` | Every `Draw*` call |
| `triangles` | `Draw*` with TRIANGLES/TRIANGLE_STRIP/TRIANGLE_FAN |
| `lines` | `Draw*` with LINES/LINE_STRIP/LINE_LOOP |
| `points` | `Draw*` with POINTS |
| `shaderSwitches` | `BindProgram` with cache miss |
| `textureBinds` | `BindTexture` with cache miss |
| `bufferBinds` | `BindBuffer` with cache miss |
| `vaoSwitches` | `BindVAO` with cache miss |
| `fboSwitches` | `BindFBO` with cache miss |
| `stateChanges` | Any `Set*` that actually changed GL state |

## GL Extension Support

The library uses the ARB compatibility profile on desktop, which exposes all
extensions as core GL 4.3. On ES targets, extensions like `EXT_color_buffer_half_float`,
`EXT_shader_texture_lod`, and `OES_shader_image_atomic` are implicitly available
via the ES 3.1 spec.

## Error Handling

The library uses a simple approach:
- Methods that can fail return `bool` (`false` on error).
- Error logs are stored in a fixed-size char array (`log[1024]`) accessible
  via `GetLog()`.
- No exceptions are thrown.

# Tests

## Running Tests

Build the test suite:

```bash
cmake -B build
cmake --build build -j
```

Run a specific test interactively:

```bash
./build/tests/test_window [test_name]
```

Run for a fixed number of frames (smoke test):

```bash
./build/tests/test_window [test_name] 120   # 120 frames, then exit
```

Press `ESC` or close the window to exit.

## Available Tests

### `clear`
Clears the screen to a solid color. Basic smoke test that the context works.

### `triangle`
Renders a rotating colored triangle. Exercises Shader, Buffer, VertexArray,
and Draw. Shows uniform updates per frame.

### `batch`
Tests the immediate-mode batcher: rectangles, circles, text rendering.
Demonstrates the embedded 8×8 font and various 2D primitives.

### `bench`
Benchmark test that renders many textured quads. Reports draw calls, triangles,
and timing. Useful for performance profiling.

### `bunny`
Bunnymark-style test: click to spawn bunnies, hold the mouse to pour them in,
press `C` to clear. Demonstrates high-throughput textured rendering.

### `shapes3d`
Renders 3D wireframe and solid shapes: cubes, spheres, cylinders, capsules,
axes, and grids. Exercises the 3D shape generation code.

### `occlusion`
Hardware occlusion queries: renders a triangle and queries how many samples
passed. Demonstrates the `Query` API.

### `rendertarget`
Creates a FrameBuffer with an attached texture, renders to it, then blits
to the screen. Exercises FBO and texture attachment.

### `verify`
Pixel-exact readback: renders a known pattern and reads back pixels with
`ReadPixels` to verify correctness.

### `compute`
Compute shader test: writes data to an SSBO via a compute shader, then
reads it back. Exercises compute dispatch and memory barriers.

## Test Infrastructure

All tests share a common boilerplate in `tests/test_common.hpp`:

- Creates an SDL2 window with an OpenGL 4.3 core context
- Initializes coregl with `SDL_GL_GetProcAddress`
- Provides event polling (quit on ESC or window close)
- Handles frame begin/end (viewport update, swap)

## Shader Examples

The `tests/Shaders/` directory contains GLSL shaders demonstrating various
rendering techniques. See [Shader Library](SHADER_LIBRARY.md) for the full list.

# Batch Shapes

The `Batch` class provides a rich set of 2D and 3D drawing primitives.
All shapes use the current color, texture, and transform state.

## 2D Shapes

### Outline Shapes (LINES)

| Method | Signature | Description |
|--------|-----------|-------------|
| `Line` | `Line(x0, y0, x1, y1)` | Single line segment |
| `Circle` | `Circle(cx, cy, r, fill, segs)` | Circle outline or filled |
| `Ellipse` | `Ellipse(cx, cy, rx, ry, fill, segs)` | Ellipse outline or filled |
| `Ring` | `Ring(cx, cy, rInner, rOuter, fill, segs)` | Ring/annulus outline or filled |
| `Arc` | `Arc(cx, cy, r, startDeg, endDeg, segs)` | Arc segment |
| `ThickLine` | `ThickLine(x0, y0, x1, y1, thickness)` | Line with thickness |
| `Polygon` | `Polygon(cx, cy, sides, r, rotDeg, fill)` | Regular polygon |
| `Polyline` | `Polyline(xyPairs, count)` | Connected line chain |
| `Grid` | `Grid(x, y, w, h, cellW, cellH)` | Rectangular grid |

### Filled Shapes (TRIANGLES)

| Method | Signature | Description |
|--------|-----------|-------------|
| `Rect` | `Rect(x, y, w, h, fill)` | Rectangle (outline or filled) |
| `Triangle` | `Triangle(x1,y1,x2,y2,x3,y3, fill)` | Triangle (outline or filled) |
| `Circle` | `Circle(cx, cy, r, true, segs)` | Filled circle (indexed fan) |
| `Ellipse` | `Ellipse(cx, cy, rx, ry, true, segs)` | Filled ellipse |
| `Ring` | `Ring(cx, cy, rInner, rOuter, true, segs)` | Filled annulus (quad strips) |
| `Polygon` | `Polygon(cx, cy, sides, r, rotDeg, true)` | Filled polygon (fan) |

### Text

| Method | Signature | Description |
|--------|-----------|-------------|
| `Text` | `Text(x, y, size, text)` | Monospace text (ASCII 32–127, `
` supported) |
| `TextWidth` | `TextWidth(size, text)` | Measure text width (no draw) |

Uses an embedded 8×8 bitmap font (public domain). The font atlas is 128×48 pixels
with 16 columns × 6 rows of 8×8 glyphs.

### Textured Quads

| Method | Signature | Description |
|--------|-----------|-------------|
| `Quad` | `Quad(tex, x, y, w, h)` | Full texture quad |
| `Quad` | `Quad(tex, sx,sy,sw,sh, x,y,w,h)` | Sub-texture quad (sprite sheet) |

## 3D Wireframe (LINES)

Use the transform stack to position/orient wireframes in 3D space.

| Method | Signature |
|--------|-----------|
| `CubeWire` | `CubeWire(cx,cy,cz, sx,sy,sz)` |
| `SphereWire` | `SphereWire(cx,cy,cz, r, rings, slices)` |
| `CylinderWire` | `CylinderWire(cx,cy,cz, r, h, slices)` |
| `CapsuleWire` | `CapsuleWire(cx,cy,cz, r, h, slices)` |
| `Grid3D` | `Grid3D(size, step)` |
| `Axes` | `Axes(size)` |

## 3D Solid (TRIANGLES, Flat Color)

| Method | Signature |
|--------|-----------|
| `Cube` | `Cube(cx,cy,cz, sx,sy,sz)` |
| `Sphere` | `Sphere(cx,cy,cz, r, rings, slices)` |
| `Cylinder` | `Cylinder(cx,cy,cz, r, h, slices)` |
| `Capsule` | `Capsule(cx,cy,cz, r, h, rings, slices)` |

## Transform Stack

Apply transforms before drawing shapes. Transforms compose in local space:
`Translate` then `Rotate` rotates around the translated origin.

| Method | Signature | Description |
|--------|-----------|-------------|
| `PushMatrix` | `PushMatrix()` | Save current transform |
| `PopMatrix` | `PopMatrix()` | Restore last saved transform |
| `LoadIdentity` | `LoadIdentity()` | Reset to identity |
| `Translate` | `Translate(x, y, z)` | Translate |
| `Rotate` | `Rotate(deg, ax, ay, az)` | Axis-angle rotation |
| `Scale` | `Scale(x, y, z)` | Scale |
| `SetTransform` | `SetTransform(mat4)` | Direct matrix set |

Max stack depth: 32 levels.

## Vertex Format

```cpp
struct Vertex {
    float x, y, z;    // position (location 0)
    float u, v;        // texture UV (location 1)
    uint32_t rgba;     // color, normalized (location 2)
};
```

Total size: 24 bytes per vertex.

## Index Format

16-bit indices (`u16`). Maximum 65,535 vertices per batch flush.

## Auto-Flush

The batch automatically flushes when the vertex or index buffer is full.
It also flushes when switching texture or primitive mode (for command merging).
