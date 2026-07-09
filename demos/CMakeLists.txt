find_package(SDL2 REQUIRED)

# demos are full programs written like final user code: node tree in,
# SceneRenderer::render() out — no direct coregl calls.
function(coregl_demo name)
    add_executable(${name} ${ARGN})
    target_include_directories(${name} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/tests # gif_recorder.hpp (single shared copy)
    )
    target_link_libraries(${name} PRIVATE scene SDL2::SDL2)
endfunction()

coregl_demo(demo_water water/main.cpp)
coregl_demo(demo_shadows shadows/main.cpp)
