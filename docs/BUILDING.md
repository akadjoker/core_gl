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
