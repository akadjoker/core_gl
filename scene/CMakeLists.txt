# scene — scene graph / engine layer, built on top of the coregl backend.
# Consumers include <scene/...> for nodes and <scene/Math.hpp> for math.
add_library(scene STATIC
    src/Node.cpp
    src/Node3D.cpp
    src/Mesh.cpp
    src/MeshLoader.cpp
    src/TerrainNode.cpp
    src/Camera3D.cpp
    src/Scene.cpp
    src/SceneRenderer.cpp
    src/WaterNode.cpp
    src/LightNode.cpp
    src/OceanNode.cpp
    src/ByteArray.cpp
    src/IO.cpp
    src/Filesystem.cpp
    src/AssetManager.cpp
    src/Color.cpp
    src/Pixmap.cpp
    src/stb_impl.cpp
)

target_include_directories(scene
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# the render-facing parts of the scene layer draw through coregl's public API
target_link_libraries(scene PUBLIC coregl)

if(MSVC)
    target_compile_options(scene PRIVATE /W4)
else()
    target_compile_options(scene PRIVATE -Wall -Wextra)
endif()
