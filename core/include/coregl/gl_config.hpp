#pragma once

// ---------------------------------------------------------------------------
// coregl — platform detection + typedefs
// The user can force the target with -DCORE_GL_FORCE_ES or -DCORE_GL_FORCE_DESKTOP
// ---------------------------------------------------------------------------

#if defined(CORE_GL_FORCE_ES)
#define CORE_GL_ES 1
#elif defined(CORE_GL_FORCE_DESKTOP)
#define CORE_GL_DESKTOP 1
#elif defined(__EMSCRIPTEN__) || defined(__ANDROID__)
#define CORE_GL_ES 1
#else
#define CORE_GL_DESKTOP 1
#endif

namespace gl
{

using u8 = unsigned char;
using u16 = unsigned short;
using u32 = unsigned int;
using u64 = unsigned long long;
using i8 = signed char;
using i16 = signed short;
using i32 = signed int;
using i64 = signed long long;
using f32 = float;
using f64 = double;

// GL function loader provided by the user:
//   gl::Renderer::Init(SDL_GL_GetProcAddress);
//   gl::Renderer::Init(glfwGetProcAddress);
//   gl::Renderer::Init(eglGetProcAddress);
using LoadProc = void* (*)(const char* name);

} // namespace gl
