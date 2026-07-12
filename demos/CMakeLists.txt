find_package(SDL2 REQUIRED)

# demos are full programs written like final user code: node tree in,
# SceneRenderer::render() out — no direct coregl calls.
function(coregl_demo name)
    add_executable(${name} ${ARGN})
    target_include_directories(${name} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/tests
    )
    target_link_libraries(${name} PRIVATE scene SDL2::SDL2)
endfunction()

coregl_demo(demo_water water/main.cpp)
coregl_demo(demo_shadows shadows/main.cpp)
coregl_demo(demo_ocean ocean/main.cpp)
coregl_demo(demo_meshes meshes/main.cpp)
coregl_demo(demo_particles particles/main.cpp)
coregl_demo(demo_terrain terrain/main.cpp)
coregl_demo(demo_terrain_tiled terrain_tiled/main.cpp)
coregl_demo(demo_terrain_infinite terrain_infinite/main.cpp)
coregl_demo(demo_terrain_lod terrain_lod/main.cpp)
coregl_demo(demo_terrain_paging terrain_paging/main.cpp)
coregl_demo(demo_character character/main.cpp)
coregl_demo(demo_primitives primitives/main.cpp)
coregl_demo(demo_sponza sponza/main.cpp)
coregl_demo(demo_powerplant powerplant/main.cpp)
coregl_demo(demo_bsp bsp/main.cpp)
coregl_demo(demo_octree_stress octree_stress/main.cpp)
coregl_demo(demo_fresnel fresnel/main.cpp)
