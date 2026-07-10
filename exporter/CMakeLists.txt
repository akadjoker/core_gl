project(exporter)
cmake_policy(SET CMP0072 NEW)


set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ")

find_package(assimp REQUIRED)

if (WIN32)
    set(LIBS_DIR "E:/windows/libs")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}   -D_CRT_SECURE_NO_WARNINGS")
    if (MSVC)
        if(CMAKE_BUILD_TYPE MATCHES Debug)
            add_compile_options(/RTC1 /Od /Zi)
            set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /fsanitize=address")
        endif()     
    endif()

endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin)

add_compile_options(
    -Wall 
)


file(GLOB SOURCES "src/*.cpp")
add_executable(exporter   ${SOURCES})

if (WIN32)
    target_include_directories(exporter PUBLIC "${LIBS_DIR}/include" include src)
else() 
    target_include_directories(exporter PUBLIC  include src)
endif()



if(CMAKE_BUILD_TYPE MATCHES Debug)

    if (UNIX)
        target_compile_options(exporter PRIVATE -fsanitize=address -fsanitize=undefined -fsanitize=leak -g  -D_DEBUG )
        target_link_options(exporter PRIVATE -fsanitize=address -fsanitize=undefined -fsanitize=leak -g  -D_DEBUG) 
    endif()


elseif(CMAKE_BUILD_TYPE MATCHES Release)
    target_compile_options(exporter PRIVATE -O3   -DNDEBUG )
    target_link_options(exporter PRIVATE -O3   -DNDEBUG )
endif()



if (WIN32)
    target_link_libraries(exporter assimp::assimp  Winmm.lib opengl32.lib)
endif()


if (UNIX)
    target_link_libraries(exporter   m assimp::assimp)
endif()

#message(STATUS "SDL2 Library Dir: ${LIB_DIR}/")