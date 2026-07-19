# scene — scene graph / engine layer, built on top of the coregl backend.
# Consumers include <scene/...> for nodes and <scene/Math.hpp> for math.
add_library(scene STATIC
    src/Node.cpp
    src/Node3D.cpp
    src/Mesh.cpp
    src/MeshLoader.cpp
    src/TerrainNode.cpp
    src/TiledTerrainNode.cpp
    src/InfiniteTerrainNode.cpp
    src/TerrainLodNode.cpp
    src/TerrainPagingNode.cpp
    src/Skeleton.cpp
    src/AnimationClip.cpp
    src/AnimationPlayer.cpp
    src/SkinnedMesh.cpp
    src/SkinnedMeshInstance.cpp
    src/BoneAttachment.cpp
    src/MeshPrimitives.cpp
    src/Camera3D.cpp
    src/Scene.cpp
    src/SceneRenderer.cpp
    src/WaterNode.cpp
    src/MirrorNode.cpp
    src/LightNode.cpp
    src/OceanNode.cpp
    src/ParticleSystemNode.cpp
    src/DecalSystemNode.cpp
    src/GrassSystemNode.cpp
    src/TreeSystemNode.cpp
    src/RibbonTrailNode.cpp
    src/BillboardNode.cpp
    src/Tree.cpp
    src/SceneOctree.cpp
    src/Collision.cpp
    src/MeshCollision.cpp
    src/World.cpp
    src/LensFlareNode.cpp
    src/ByteArray.cpp
    src/IO.cpp
    src/Filesystem.cpp
    src/AssetManager.cpp
    src/Color.cpp
    src/Pixmap.cpp
    src/BSPLoader.cpp
    src/MorphAnimation.cpp
    src/MD2Loader.cpp
    src/MD3Loader.cpp
    src/MorphMeshInstance.cpp
    src/TagAttachment.cpp
    src/GLTFLoader.cpp
    src/B3DLoader.cpp
    src/IQMLoader.cpp
    src/C3DSLoader.cpp
    src/MS3DLoader.cpp
    src/Input.cpp
    src/Behavior.cpp
    src/FreeFlyBehavior.cpp
    src/MayaBehavior.cpp
    src/OrbitBehavior.cpp
    src/OscillateBehavior.cpp
    src/RotatorBehavior.cpp
    src/FollowBehavior.cpp
    src/CharacterBehavior.cpp
    src/CSG.cpp
    src/VolumeSource.cpp
    src/VolumeNoise.cpp
    src/VolumeCSGSource.cpp
    src/VolumeGridSource.cpp
    src/VolumeMesher.cpp
    src/stb_impl.cpp
    src/cgltf_impl.cpp
    src/miniz_impl.cpp
)

target_include_directories(scene
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# the render-facing parts of the scene layer draw through coregl's public API
target_link_libraries(scene PUBLIC coregl)

# Input.hpp (Behavior nodes' input source) polls SDL2 state directly; every
# consumer of scene already links SDL2 for its own window/event loop (see
# demos/CMakeLists.txt), so this is just making that dependency explicit.
find_package(SDL2 REQUIRED)
target_link_libraries(scene PUBLIC SDL2::SDL2)

if(MSVC)
    target_compile_options(scene PRIVATE /W4)
else()
    target_compile_options(scene PRIVATE -Wall -Wextra)
endif()
