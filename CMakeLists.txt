cmake_minimum_required(VERSION 3.20)
project(SuperMarioAdvance4Recomp VERSION 0.0.4 LANGUAGES C CXX)
set(GAME_BUILD_VERSION "${PROJECT_VERSION}" CACHE STRING "Packaged game version")

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(GBARECOMP_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/gbarecomp"
    CACHE PATH "Path to this game's gbarecomp worktree")
set(RECOMP_UI_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/recomp-ui"
    CACHE PATH "Path to the shared recomp-ui checkout")
set(GBARECOMP_RUNTIME_UI_ROOT "${RECOMP_UI_ROOT}"
    CACHE PATH "recomp-ui root used by the live GBA runtime adapter")
if(NOT EXISTS "${GBARECOMP_ROOT}/CMakeLists.txt")
    message(FATAL_ERROR
        "GBARECOMP_ROOT is not a gbarecomp checkout:\n"
        "  ${GBARECOMP_ROOT}\n"
        "Configure with -DGBARECOMP_ROOT=<path> if the local worktree link is absent.")
endif()

set(GBARECOMP_ENABLE_MODS ON CACHE BOOL "Enable the title mod catalog" FORCE)
set(GBARECOMP_ENABLE_HQ2X ON CACHE BOOL "Include the optional HQ2x display mod" FORCE)
add_subdirectory("${GBARECOMP_ROOT}" gbarecomp_build EXCLUDE_FROM_ALL)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/generated/recompiled.cpp")
    message(FATAL_ERROR
        "Stale monolithic generated/recompiled.cpp found. "
        "Run tools/regen.ps1 with the current gbarecomp worktree.")
endif()
file(GLOB GAME_GENERATED_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/generated/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/generated/*.cpp")

add_executable(SuperMarioAdvance4Recomp
    src/main.cpp
    src/smb3_extended_view.cpp
    src/smb3_entity_view.cpp
    src/mods/smb3_adaptive_view_plugin.cpp
    ${GAME_GENERATED_SOURCES})
gbarecomp_stage_hq2x(SuperMarioAdvance4Recomp)

set(GAME_RECOMPILED_SOURCES ${GAME_GENERATED_SOURCES})
list(FILTER GAME_RECOMPILED_SOURCES INCLUDE REGEX
    "/recompiled_[0-9]+\\.cpp$")
if(GAME_RECOMPILED_SOURCES)
    set_source_files_properties(${GAME_RECOMPILED_SOURCES} PROPERTIES
        COMPILE_OPTIONS "-O1;-g0")
endif()

target_include_directories(SuperMarioAdvance4Recomp PRIVATE
    "${GBARECOMP_ROOT}/src/armv4t"
    "${GBARECOMP_ROOT}/src/gba"
    "${GBARECOMP_ROOT}/src/runtime"
    "${GBARECOMP_ROOT}/src/debug")

add_custom_command(TARGET SuperMarioAdvance4Recomp POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/mods/preloaded"
        "$<TARGET_FILE_DIR:SuperMarioAdvance4Recomp>/mods")

target_link_libraries(SuperMarioAdvance4Recomp PRIVATE
    "-Wl,--start-group"
    gbarecomp_runtime
    gbarecomp_debug
    gbarecomp_gba
    gbarecomp_armv4t
    "-Wl,--end-group")

set_property(SOURCE src/main.cpp APPEND PROPERTY COMPILE_DEFINITIONS
    GBARECOMP_GAME_VERSION="${GAME_BUILD_VERSION}")

target_compile_definitions(SuperMarioAdvance4Recomp PRIVATE
    GBARECOMP_DEFAULT_DEBUG_PORT=19883
    GBARECOMP_WINDOW_TITLE="SuperMarioAdvance4Recomp"
    GBARECOMP_DEFAULT_GAME_CONFIG="game.toml")

enable_testing()
add_executable(sma4_view_tests
    tests/extended_view_test.cpp
    src/smb3_extended_view.cpp
    "${GBARECOMP_ROOT}/tools/bios_smoke/dispatch_stub.cpp")
target_include_directories(sma4_view_tests PRIVATE
    src
    "${GBARECOMP_ROOT}/src/runtime")
target_link_gbarecomp_runtime_stack(sma4_view_tests)
add_test(NAME sma4_view_tests COMMAND sma4_view_tests)

option(GBAGAME_RECOMP_UI "Build the recomp-ui launcher and in-game menu" ON)
if(GBAGAME_RECOMP_UI)
    if(NOT EXISTS "${RECOMP_UI_ROOT}/recomp_ui.cmake")
        message(FATAL_ERROR
            "recomp-ui is not available at ${RECOMP_UI_ROOT}. "
            "Run git submodule update --init --recursive or configure with "
            "-DRECOMP_UI_ROOT=<path>.")
    endif()
    include("${RECOMP_UI_ROOT}/recomp_ui.cmake")
    add_library(game_launcher_ui STATIC src/game_launcher_boot.cpp)
    target_include_directories(game_launcher_ui PRIVATE
        "${GBARECOMP_ROOT}/src/runtime"
        "${GBARECOMP_ROOT}/src/gba"
        src)
    recomp_target_launcher_ui(game_launcher_ui CONSOLE gba)
    target_compile_definitions(game_launcher_ui PRIVATE GBARECOMP_ENABLE_MODS=1)
    find_package(SDL2 REQUIRED)
    target_link_libraries(game_launcher_ui PUBLIC SDL2::SDL2)
    target_link_libraries(SuperMarioAdvance4Recomp PRIVATE game_launcher_ui)
    set_property(SOURCE src/main.cpp APPEND PROPERTY
        COMPILE_DEFINITIONS GBAGAME_RECOMP_UI)
endif()

if(MSVC)
    target_link_options(SuperMarioAdvance4Recomp PRIVATE /STACK:16777216)
elseif(MINGW OR WIN32)
    target_link_options(SuperMarioAdvance4Recomp PRIVATE -Wl,--stack,16777216)
endif()
