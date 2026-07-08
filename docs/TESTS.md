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
