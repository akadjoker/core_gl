find_package(SDL2 REQUIRED)

add_executable(test_window main.cpp)
target_link_libraries(test_window PRIVATE coregl scene SDL2::SDL2)
