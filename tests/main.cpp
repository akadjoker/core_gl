// Test runner: picks a test by name.
// Usage: test_window [test] [numFrames]
//   test       — see the list below (default: triangle)
//   numFrames  — run N frames and exit (smoke test); 0 or absent = interactive
//
// Press F10 during any test to start/stop recording a GIF of the window.

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
#include "test_perf.hpp"
#include "test_quad_verify.hpp"
#include "test_gbuffer_view.hpp"
#include "test_winding_verify.hpp"
#include "test_blit_verify.hpp"
#include "test_ubo_verify.hpp"
#include "test_texturearray_verify.hpp"
#include "test_msaa_verify.hpp"
#include "test_shadowmap.hpp"
#include "test_csm.hpp"
#include "test_scene.hpp"
#include "test_base_components.hpp"
#include "tutorial_01_triangle.hpp"
#include "tutorial_02_texture.hpp"
#include "tutorial_03_cube.hpp"
#include "tutorial_04_plane.hpp"
#include <cstring>
#include <cstdlib>

struct NamedTest
{
    const char* name;
    int (*run)(int maxFrames);
    const char* description;
};

static const NamedTest kTests[] = {
    {"tut1", tutorial_01_triangle, "tutorial: hello triangle (Shader/Buffer/VertexArray)"},
    {"tut2", tutorial_02_texture, "tutorial: textured quad (Texture, indexed draw)"},
    {"tut3", tutorial_03_cube, "tutorial: rotating cube (depth test, MVP matrix)"},
    {"tut4", tutorial_04_plane, "tutorial: tiled ground plane + cube (shared camera)"},

    {"clear", test_clear, "animated clear color (state cache)"},
    {"triangle", test_triangle, "rotating triangle"},
    {"occlusion", test_occlusion, "hardware occlusion query"},
    {"rendertarget", test_rendertarget, "render-to-texture + post-process pass"},
    {"gbufferview", test_gbuffer_view, "MRT G-buffer with a multi-quad debug view"},
    {"compute", test_compute, "compute shader + SSBO round-trip"},

    {"batch", test_batch, "2D/3D batch: shapes, text, transform stack"},
    {"shapes3d", test_shapes3d, "batch solids and wireframes with perspective"},
    {"bunny", test_bunnymark, "bunnymark (hold mouse buttons to spawn, C clears)"},
    {"bench", test_bench, "batch throughput benchmark"},
    {"perf", test_perf, "fixed-count bunnymark for automated perf runs"},

    {"verify", test_batch_verify, "pixel-exact checks: batch geometry and text"},
    {"quadverify", test_quad_verify, "pixel-exact checks: DrawQuad placement"},
    {"windingverify", test_winding_verify, "checks triangle winding on every solid shape"},
    {"blitverify", test_blit_verify, "pixel-exact checks: BlitFramebuffer scaling and rects"},
    {"uboverify", test_ubo_verify, "pixel-exact checks: a UBO shared by two shaders"},
    {"arrayverify", test_texturearray_verify,
     "pixel-exact checks: texture arrays + layer render targets"},
    {"msaaverify", test_msaa_verify, "checks MSAA resolve actually blends edges"},
    {"shadowmap", test_shadowmap, "plane + cube with a real-time orbiting-light shadow map"},
    {"scene", test_scene, "node-tree scene rendered through collected RenderItems"},
    {"csm", test_csm, "cascaded shadow maps (first step)"},
    {"base", test_base_components, "ByteArray + Filesystem + AssetManager validation"},

};
static const int kTestCount = (int)(sizeof(kTests) / sizeof(kTests[0]));

static void printUsage()
{
    fprintf(stderr, "usage: test_window [test] [numFrames]\n\navailable tests:\n");
    for (int i = 0; i < kTestCount; ++i)
        fprintf(stderr, "  %-14s %s\n", kTests[i].name, kTests[i].description);
}

int main(int argc, char** argv)
{
    const char* name = (argc > 1) ? argv[1] : "triangle";
    int maxFrames = (argc > 2) ? atoi(argv[2]) : 0;

    if (strcmp(name, "-h") == 0 || strcmp(name, "--help") == 0)
    {
        printUsage();
        return 0;
    }

    for (int i = 0; i < kTestCount; ++i)
        if (strcmp(name, kTests[i].name) == 0) return kTests[i].run(maxFrames);

    fprintf(stderr, "unknown test '%s'\n\n", name);
    printUsage();
    return 1;
}
