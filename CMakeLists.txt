cmake_minimum_required(VERSION 3.16)
project(coregl LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

option(COREGL_BUILD_TESTS "Build coregl tests" ON)

add_subdirectory(core)

if(COREGL_BUILD_TESTS)
    add_subdirectory(tests)
endif()
