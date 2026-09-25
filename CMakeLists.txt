cmake_minimum_required(VERSION 3.16)
project(Depth C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Download and build raylib automatically (first build needs internet + git).
include(FetchContent)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_GAMES OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    raylib
    GIT_REPOSITORY https://github.com/raysan5/raylib.git
    GIT_TAG 5.5
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(raylib)

add_executable(depth
    src/main.cpp
    src/data.cpp
    src/ui.cpp
    src/render.cpp
    src/hub.cpp
    src/dungeon.cpp
    src/platformer.cpp
)
target_link_libraries(depth PRIVATE raylib)
