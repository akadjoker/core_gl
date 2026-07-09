#pragma once

// ---------------------------------------------------------------------------
// INTERNAL — never installed nor visible to the user.
// Includes the official platform GL headers, with guards.
// Only the library .cpp files include this header.
// ---------------------------------------------------------------------------

#include "coregl/gl_config.hpp"

#if defined(CORE_GL_ES)
// Mobile / WebGL2: OpenGL ES 3.1 (compute + SSBO); prototypes included
#include <GLES3/gl31.h>
#elif defined(__APPLE__)
// macOS: core profile (4.1 max — no compute)
#define GL_SILENCE_DEPRECATION 1
#include <OpenGL/gl3.h>
#elif defined(_WIN32)
// Windows only exports GL 1.1 — needs a runtime loader (wglGetProcAddress).
// TODO: support it when a Windows target is needed.
#error "coregl: Windows target not supported yet"
#else
// Linux desktop: glcorearb.h with prototypes; libGL/libOpenGL exports everything
#ifndef GL_GLEXT_PROTOTYPES
#define GL_GLEXT_PROTOTYPES 1
#endif
#include <GL/glcorearb.h>
#endif

// ES headers only expose clip distances through EXT_clip_cull_distance; the
// enum values are shared with desktop GL, so define them when missing.
#ifndef GL_CLIP_DISTANCE0
#define GL_CLIP_DISTANCE0 0x3000
#endif
