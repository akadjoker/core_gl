find_package(SDL2 REQUIRED)

# demos are full programs written like final user code: node tree in,
# SceneRenderer::render() out — no direct coregl calls.
add_executable(demo_water water/main.cpp)
target_include_directories(demo_water PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/tests # gif_recorder.hpp (single shared copy)
)
target_link_libraries(demo_water PRIVATE scene SDL2::SDL2)
