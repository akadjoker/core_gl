# Zeke On Your Six! — standalone game, own src/ and assets/, built the same
# way hoverjet is: against the scene library, reusing demos/'s DemoApp/gif
# recorder rather than duplicating that boilerplate.
add_executable(zekeonyoursix src/main.cpp src/audio.cpp)
target_include_directories(zekeonyoursix PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/demos
    ${CMAKE_SOURCE_DIR}/tests
)
find_package(Threads REQUIRED)
target_link_libraries(zekeonyoursix PRIVATE scene SDL2::SDL2 Threads::Threads ${CMAKE_DL_LIBS})
