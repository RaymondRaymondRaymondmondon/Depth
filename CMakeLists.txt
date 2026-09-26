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
    src/salon.cpp
    src/save.cpp
    src/dungeon.cpp
    src/platformer.cpp
    src/flats.cpp
    src/flats_card.cpp
    src/flats_board.cpp
    src/flats_run.cpp
    src/flats_sim.cpp
    src/skeletal_node.cpp
    src/sprite_renderer.cpp
    src/relics.cpp
    src/levelgen.cpp
    src/enemyart.cpp
)
target_link_libraries(depth PRIVATE raylib)
