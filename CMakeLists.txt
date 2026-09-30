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
    src/flats_art.cpp
    src/skeletal_node.cpp
    src/sprite_renderer.cpp
    src/relics.cpp
    src/levelgen.cpp
    src/enemyart.cpp
    src/chargen.cpp
    src/abyss.cpp
    src/beasts.cpp
    src/beasts_biomes.cpp src/beasts_hull.cpp src/beasts_pirate.cpp src/beasts_cave.cpp src/beasts_island.cpp src/beasts_weeds.cpp src/beasts_atlantis.cpp src/dossier.cpp src/sound.cpp src/sound_parkour.cpp src/pixelart.cpp src/beastart.cpp
    src/rig.cpp src/rig_clips.cpp src/rigfigs.cpp src/chart.cpp src/combatfx.cpp src/input.cpp src/menu.cpp
    src/json.cpp src/redtide_data.cpp src/redtide_eco.cpp src/redtide_tools.cpp src/redtide_render.cpp src/redtide_game.cpp
    src/redtide_match.cpp src/redtide_profile.cpp src/redtide_menu.cpp
)
target_link_libraries(depth PRIVATE raylib)

# The arcade games' data (data/redtide, data/trawl) is read at run time: copy it next to the executable.
add_custom_command(TARGET depth POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/data $<TARGET_FILE_DIR:depth>/data)
