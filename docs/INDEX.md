# coregl Documentation

Lightweight OpenGL/OpenGL ES abstraction layer — zero external dependencies,
cross-platform (Desktop GL 4.3+ and OpenGL ES 3.1/WebGL2).

A fast, low-level OpenGL render backend in C++11. No dependencies, no exceptions,
no smart pointers — plain C-style API over a cached GL state machine. This is
**not** an engine: it is the backend an engine is built on.

## Quick Start

```cpp
#include <coregl/gl_core.hpp>

// Initialize with your GL loader function
gl::Renderer::Init(SDL_GL_GetProcAddress);  // or glfwGetProcAddress, eglGetProcAddress...

// Create a batch renderer (immediate-mode 2D/3D)
gl::Batch batch;
batch.Init(65535);
batch.SetProjection(ortho);                 // raw float[16], bring your own math

batch.SetColor(255, 80, 80, 255);
batch.Rect(40, 40, 120, 80);
batch.Text(40, 140, 16, "hello from the embedded font");
batch.Render();
```

## Table of Contents

- **[Building](BUILDING.md)** — Prerequisites, build commands, platform notes
- **[API Reference](API_REFERENCE.md)** — Complete class/method/enum reference
- **[Architecture](ARCHITECTURE.md)** — State cache, uniform cache, lifetime management
- **[Tests](TESTS.md)** — Available tests and how to run them
- **[Batch Shapes](BATCH_SHAPES.md)** — Catalog of 2D and 3D drawing primitives
- **[Scene Nodes](SCENE.md)** — Node hierarchy, scene graph, rendering pipeline, VFX, terrain, CSG, behaviors, math library

## Supported Platforms

| Target        | API           | Status                                  |
|---------------|---------------|-----------------------------------------|
| Linux desktop | OpenGL 4.3+   | Working (primary target)                |
| Android       | OpenGL ES 3.1 | Compiles (`-DCORE_GL_FORCE_ES`)         |
| Web           | WebGL2/ES 3.0 | Compiles; no compute/geometry           |
| macOS         | OpenGL 4.1    | Compiles; no compute                    |
| Windows       | --             | Not yet (needs a runtime loader)        |

## Key Features

- **Renderer** — Central GL state cache: every state change and bind is compared
  against the cache and only reaches the driver when it actually changes.
  Stats for everything: draw calls, triangles, lines, binds, state changes.
- **Shader** — Vertex/geometry/fragment/compute. All active uniforms are
  introspected once at `Link()`; uniform lookups never touch the driver again.
  Hot path setters by location, convenience setters by name (FNV-1a hash).
- **Buffer / VertexArray** — VBO, IBO (u16/u32), SSBO, UBO, instancing.
- **Texture / FrameBuffer** — 2D, cubemap, depth, depth cubemap; MRT up to 16
  color attachments, depth-only mode for shadow maps.
- **Query** — Hardware occlusion queries (samples passed), timers, primitives.
- **Batch** — Indexed immediate-mode renderer with transform stack, 2D/3D shapes,
  embedded 8×8 font, and automatic draw merging.

## Numbers (AMD Renoir iGPU, Mesa)

- **50,000 textured quads**: 3.5 ms CPU submit — **14.3 M quads/s**
- **Bunnymark**: 100,000 bunnies at **90 fps**; 353k at 32 fps (fill-bound)
- 120 frames of a spinning triangle: **1** shader switch, **1** VAO switch

## License

MIT — see the root `README.md` for details.
