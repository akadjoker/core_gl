// Test runner: picks a test by name.
// Usage: test_window [test] [numFrames]
//   test       — "clear" | "triangle" (default: triangle)
//   numFrames  — run N frames and exit (smoke test); 0 or absent = interactive

#include "test_clear.hpp"
#include "test_triangle.hpp"
#include "test_occlusion.hpp"
#include "test_rendertarget.hpp"
#include "test_batch.hpp"
#include "test_batch_verify.hpp"
#include "test_bench.hpp"
#include "test_bunnymark.hpp"
#include "test_shapes3d.hpp"
#include "test_compute.hpp"
#include <cstring>
#include <cstdlib>

int main(int argc, char** argv)
{
    const char* name = (argc > 1) ? argv[1] : "triangle";
    int maxFrames = (argc > 2) ? atoi(argv[2]) : 0;

    if (strcmp(name, "clear") == 0) return test_clear(maxFrames);
    if (strcmp(name, "triangle") == 0) return test_triangle(maxFrames);
    if (strcmp(name, "occlusion") == 0) return test_occlusion(maxFrames);
    if (strcmp(name, "rendertarget") == 0) return test_rendertarget(maxFrames);
    if (strcmp(name, "batch") == 0) return test_batch(maxFrames);
    if (strcmp(name, "verify") == 0) return test_batch_verify(maxFrames);
    if (strcmp(name, "bench") == 0) return test_bench(maxFrames);
    if (strcmp(name, "bunny") == 0) return test_bunnymark(maxFrames);
    if (strcmp(name, "shapes3d") == 0) return test_shapes3d(maxFrames);
    if (strcmp(name, "compute") == 0) return test_compute(maxFrames);

    fprintf(stderr,
            "unknown test '%s' — available: clear, triangle, occlusion, rendertarget, batch\n",
            name);
    return 1;
}
