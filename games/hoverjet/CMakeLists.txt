# Hoverjet Racing — standalone game, not a demo: its own src/ and assets/,
# built like real user code against the scene library. Reuses demos/'s
# DemoApp (SDL2/GL boilerplate) and gif recorder rather than duplicating them.
add_executable(hoverjet src/main.cpp src/audio.cpp)
target_include_directories(hoverjet PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/demos
    ${CMAKE_SOURCE_DIR}/tests
)
find_package(Threads REQUIRED)
target_link_libraries(hoverjet PRIVATE scene SDL2::SDL2 Threads::Threads ${CMAKE_DL_LIBS})
