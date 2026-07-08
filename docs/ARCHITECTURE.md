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
