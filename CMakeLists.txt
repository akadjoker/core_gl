cmake_minimum_required(VERSION 3.16)
project(coregl LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

option(COREGL_BUILD_TESTS "Build coregl tests" ON)
option(COREGL_BUILD_DEMOS "Build coregl demos" ON)
option(COREGL_BUILD_GAMES "Build coregl games" ON)

add_subdirectory(core)
add_subdirectory(scene)

if(COREGL_BUILD_TESTS)
    add_subdirectory(tests)
    add_subdirectory(scene_tests)
endif()

if(COREGL_BUILD_DEMOS)
    add_subdirectory(demos)
endif()

if(COREGL_BUILD_GAMES)
    add_subdirectory(games)
endif()
