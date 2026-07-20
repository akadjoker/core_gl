# Urban Tactics — test scene: converted-offline BSP city level (see
# tools/bspx_to_h3d.py) + a genuine Q3 player model, built the same way
# hoverjet/zekeonyoursix are: own src/ and assets/, against the scene
# library, reusing demos/'s DemoApp/gif recorder.
add_executable(urbantactics src/main.cpp)
target_include_directories(urbantactics PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/demos
    ${CMAKE_SOURCE_DIR}/tests
)
find_package(Threads REQUIRED)
target_link_libraries(urbantactics PRIVATE scene SDL2::SDL2 Threads::Threads ${CMAKE_DL_LIBS})
