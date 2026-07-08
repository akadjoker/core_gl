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

Run `./build/tests/test_window --help` (or with no arguments and a bad test
name) for the authoritative, always-current list — the summaries below give
context for each one.

### `tut1`, `tut2`, `tut3`, `tut4`
Tutorials, in order: a hello-triangle (Shader/Buffer/VertexArray/Draw), a
textured quad (Texture, indexed draw), a rotating cube (depth test, MVP
uniform), and a tiled ground plane with the cube sitting on it (shared
camera, `TextureWrap::REPEAT`). Each is self-contained and heavily commented.

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

### `gbufferview`
Renders albedo + a second buffer via MRT into an offscreen target, then
composites both plus a depth buffer as three independently positioned quads
on screen with `Renderer::DrawQuad()` — one `Viewport()` call for the whole
composite pass. The pattern for any multi-view debug HUD.

### `quadverify`
Pixel-exact readback: draws two differently sized `DrawQuad()` rects into
one target without touching `Viewport()`/`Scissor()` between them, and
asserts each one landed exactly where its own uniforms said it should.

### `windingverify`
Renders the tutorial cube and every `Batch` solid (cube, sphere, cylinder,
capsule) with `CullMode::NONE` and `CullMode::BACK` from nine angles,
asserting identical lit-pixel counts. A mismatch means a face has reversed
winding and is being wrongly discarded by backface culling.

### `perf`
Fixed-count bunnymark (no mouse interaction) for scripted/automated
performance runs.

### `bench`, `verify` (already listed above) round out the automated checks.

## Recording a GIF

Press **F10** during any test to start recording a GIF of the window;
press it again to stop and save `coregl_capture_<timestamp>.gif` in the
current directory. Set `COREGL_RECORD=1` in the environment to start
recording automatically when the test opens — useful for scripted captures,
e.g. `COREGL_RECORD=1 ./build/tests/test_window batch 180`. See
`tests/gif_recorder.hpp`.

## Test Infrastructure

All tests share a common boilerplate in `tests/test_common.hpp`:

- Creates an SDL2 window with an OpenGL 4.3 core context
- Initializes coregl with `SDL_GL_GetProcAddress`
- Provides event polling (quit on ESC or window close)
- Handles frame begin/end (viewport update, swap)

