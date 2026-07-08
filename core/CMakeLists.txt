add_library(coregl STATIC
    src/gl_renderer.cpp
    src/gl_shader.cpp
    src/gl_buffer.cpp
    src/gl_vertex_array.cpp
    src/gl_texture.cpp
    src/gl_renderbuffer.cpp
    src/gl_framebuffer.cpp
    src/gl_batch.cpp
)

# Public headers: the user includes <coregl/gl_core.hpp>
target_include_directories(coregl
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# Link against the platform GL library
if(ANDROID)
    target_link_libraries(coregl PRIVATE GLESv3)
elseif(EMSCRIPTEN)
    # emscripten resolves GL via -sFULL_ES3 / -sMAX_WEBGL_VERSION=2 on the executable
else()
    find_package(OpenGL REQUIRED)
    target_link_libraries(coregl PRIVATE OpenGL::GL)
endif()

if(MSVC)
    target_compile_options(coregl PRIVATE /W4)
else()
    target_compile_options(coregl PRIVATE -Wall -Wextra)
endif()
