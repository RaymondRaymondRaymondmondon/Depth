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
# JPEG textures inside glTF (tools/artgen bakes the Trawl's guns and props with JPEG texture sets to keep them small)
target_compile_definitions(raylib PRIVATE SUPPORT_FILEFORMAT_JPG=1)

add_executable(depth
    src/main.cpp
src/voice.cpp
src/voice_test.cpp
src/trawl_voice.cpp
src/flight.cpp
src/flight_game.cpp
src/flight_colony.cpp
src/flight_map.cpp
src/flight_war.cpp
src/flight_net.cpp
src/flight_society.cpp
src/flight_danger.cpp
src/flight_costumes.cpp
src/flight_long.cpp
    src/flight_isles.cpp
    src/flight_longflight.cpp
    src/mouthful.cpp
    src/mouthful_tools.cpp
    src/mouthful_game.cpp
    src/mouthful_net.cpp
    src/mouthful_events.cpp
    src/mouthful_skins.cpp
    src/nightoff.cpp
    src/nightoff_patrons.cpp
    src/nightoff_games.cpp
    src/nightoff_tables.cpp
    src/nightoff_gamesui.cpp
    src/nightoff_brawl.cpp
    src/nightoff_flirt.cpp
    src/nightoff_bots.cpp
    src/nightoff_net.cpp
    src/nightoff_events.cpp
    src/nightoff_cards.cpp
    src/nightoff_cardroom.cpp
    src/nightoff_profile.cpp
    src/nightoff_wares.cpp
    src/nightoff_monkey.cpp
    src/nightoff_seasons.cpp
    src/nightoff_skins.cpp
    src/nightoff_game.cpp
    src/scuffle.cpp
    src/scuffle_arms.cpp
    src/scuffle_match.cpp
    src/scuffle_stage.cpp
    src/scuffle_pieces.cpp
    src/scuffle_packs.cpp
    src/scuffle_game.cpp
    src/scuffle_net.cpp
    src/scuffle_hazards.cpp
    src/scuffle_worlds.cpp
    src/scuffle_build.cpp
    src/scuffle_special.cpp
    src/scuffle_arsenal.cpp
    src/scuffle_rules.cpp
    src/scuffle_modes.cpp
    src/scuffle_boss.cpp
    src/scuffle_cosmetics.cpp
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
    src/rig.cpp src/rig_clips.cpp src/rigfigs.cpp src/chart.cpp src/combatfx.cpp src/input.cpp src/menu.cpp src/warp.cpp src/warp_bots.cpp src/warp_game.cpp src/warp_net.cpp src/fowl.cpp src/fowl_shop.cpp src/fowl_bots.cpp src/fowl_net.cpp src/fowl_art.cpp src/fowl_game.cpp src/howto.cpp
    src/scuttle.cpp src/net_gns.cpp src/net_lan.cpp src/net_mem.cpp src/arcade_session.cpp src/arcade_games.cpp src/arcade.cpp src/net_test.cpp
    src/study_audio.cpp src/study_data.cpp src/study_test.cpp src/study.cpp src/study_scenes.cpp
    src/json.cpp src/expr.cpp src/course.cpp
    src/redtide_data.cpp src/redtide_eco.cpp src/redtide_tools.cpp src/redtide_render.cpp src/figure3d.cpp src/skins.cpp src/skins_data.cpp src/skins_ui.cpp src/redtide_vis.cpp src/redtide_game.cpp
    src/redtide_match.cpp src/redtide_net.cpp src/redtide_profile.cpp src/redtide_menu.cpp
    src/trawl_data.cpp src/trawl_boat.cpp src/trawl_fish.cpp src/trawl_eco.cpp src/trawl_session.cpp src/trawl_gear.cpp src/trawl_art.cpp src/trawl.cpp src/trawl_view3d.cpp src/trawl_bots.cpp src/trawl_net.cpp src/trawl_sonar.cpp src/trawl_weapons.cpp src/trawl_sim.cpp src/trawl_skiff.cpp src/trawl_landing.cpp src/trawl_quest.cpp src/trawl_below.cpp src/trawl_weeds.cpp src/trawl_grotto.cpp src/trawl_atlantis.cpp src/trawl_wreck.cpp src/trawl_dive.cpp src/trawl_divescene.cpp
)
target_link_libraries(depth PRIVATE raylib)

# The Deep Arcade's networking (docs/design/5_Depth_Arcade_Networking.md): Valve's GameNetworkingSockets, built once
# into external/gns by tools/build_gns.ps1. Without it Depth still builds; the arcade says networking is off.
set(DEPTH_GNS_DIR "${CMAKE_SOURCE_DIR}/external/gns")
set(DEPTH_VCPKG_DIR "${CMAKE_SOURCE_DIR}/external/vcpkg_installed/x64-windows-static-md")
if(EXISTS "${DEPTH_GNS_DIR}/lib/cmake/GameNetworkingSockets")
    list(APPEND CMAKE_PREFIX_PATH "${DEPTH_GNS_DIR}" "${DEPTH_VCPKG_DIR}")
    set(CMAKE_FIND_PACKAGE_PREFER_CONFIG ON)
    # protobuf's package config insists on protoc even though Depth never runs it: point it at vcpkg's host build
    file(GLOB_RECURSE DEPTH_PROTOC "${CMAKE_SOURCE_DIR}/external/vcpkg_installed/x64-windows/tools/protobuf/protoc*.exe")
    if(DEPTH_PROTOC)
        list(GET DEPTH_PROTOC 0 DEPTH_PROTOC)
        set(Protobuf_PROTOC_EXECUTABLE "${DEPTH_PROTOC}" CACHE FILEPATH "" FORCE)
    endif()
    find_package(GameNetworkingSockets CONFIG REQUIRED)
    target_link_libraries(depth PRIVATE GameNetworkingSockets::GameNetworkingSockets_s)
    target_compile_definitions(depth PRIVATE DEPTH_HAVE_GNS STEAMNETWORKINGSOCKETS_STATIC_LINK)
    message(STATUS "Depth: networking ON (GameNetworkingSockets)")
else()
    message(STATUS "Depth: networking OFF (run tools/build_gns.ps1 to build GameNetworkingSockets)")
endif()
target_link_libraries(depth PRIVATE ws2_32)

# the build id every peer compares in the handshake: changes whenever the sources are reconfigured/rebuilt
string(TIMESTAMP DEPTH_BUILD_STAMP "%Y%m%d%H%M%S" UTC)
target_compile_definitions(depth PRIVATE DEPTH_BUILD_STAMP="${DEPTH_BUILD_STAMP}")
# The arcade games' data (data/redtide, data/trawl) is read at run time: copy it next to the executable.
add_custom_command(TARGET depth POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/data $<TARGET_FILE_DIR:depth>/data)

