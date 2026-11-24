cmake_minimum_required(VERSION 3.20)
project(SuperMarioAdvance4Recomp VERSION 0.0.9 LANGUAGES C CXX)
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

# Desktop packages provide libarchive; unsupported/mobile builds retain raw input.
if(WIN32 OR (UNIX AND NOT ANDROID AND NOT EMSCRIPTEN))
    set(GBARECOMP_ROM_ARCHIVES ON CACHE BOOL "Read ZIP/RAR/7z ROM containers")
endif()
set(GBARECOMP_ENABLE_MODS ON CACHE BOOL "Enable the title mod catalog" FORCE)
set(GBARECOMP_ENABLE_HQ2X ON CACHE BOOL "Include the optional HQx display mod" FORCE)
add_subdirectory("${GBARECOMP_ROOT}" gbarecomp_build EXCLUDE_FROM_ALL)
# One executable per generated corpus. The stock build is always configured.
# The VC Edition build (game_vc.toml: stock ROM + VC Edition xdelta, which
# restores the e-Reader-only content) is added once tools/regen.ps1 -Variant vc
# has populated generated_vc/ and game_vc.toml carries the pinned identity.
function(sma4_add_game target generated_dir game_config debug_port window_title)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${generated_dir}/recompiled.cpp")
        message(FATAL_ERROR
            "Stale monolithic ${generated_dir}/recompiled.cpp found. "
            "Run tools/regen.ps1 with the current gbarecomp worktree.")
    endif()
    file(GLOB generated_sources CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/${generated_dir}/*.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/${generated_dir}/*.cpp")

    add_executable(${target}
        src/main.cpp
        src/smb3_extended_view.cpp
        src/smb3_entity_view.cpp
        src/mods/smb3_adaptive_view_plugin.cpp
        ${generated_sources})
    gbarecomp_stage_hq2x(${target})
    if(TARGET gbarecomp_hqx)
        target_sources(${target} PRIVATE src/mods/hqx_plugin.cpp)
    endif()

    set(recompiled_sources ${generated_sources})
    list(FILTER recompiled_sources INCLUDE REGEX
        "/recompiled_[0-9]+\\.cpp$")
    if(recompiled_sources)
        set_source_files_properties(${recompiled_sources} PROPERTIES
            COMPILE_OPTIONS "-O1;-g0")
    endif()

    target_include_directories(${target} PRIVATE
        "${GBARECOMP_ROOT}/src/armv4t"
        "${GBARECOMP_ROOT}/src/gba"
        "${GBARECOMP_ROOT}/src/runtime"
        "${GBARECOMP_ROOT}/src/debug")

    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/mods/preloaded"
            "$<TARGET_FILE_DIR:${target}>/mods")

    target_link_libraries(${target} PRIVATE
        "-Wl,--start-group"
        gbarecomp_runtime
        gbarecomp_debug
        gbarecomp_gba
        gbarecomp_armv4t
        "-Wl,--end-group")

    target_compile_definitions(${target} PRIVATE
        GBARECOMP_DEFAULT_DEBUG_PORT=${debug_port}
        GBARECOMP_WINDOW_TITLE="${window_title}"
        GBARECOMP_DEFAULT_GAME_CONFIG="${game_config}")

    if(MSVC)
        target_link_options(${target} PRIVATE /STACK:16777216)
    elseif(MINGW OR WIN32)
        target_link_options(${target} PRIVATE -Wl,--stack,16777216)
    endif()
endfunction()

sma4_add_game(SuperMarioAdvance4Recomp generated game.toml 19883
    "SuperMarioAdvance4Recomp")
set(SMA4_GAME_TARGETS SuperMarioAdvance4Recomp)

set(SMA4_VC_ROM_SHA1 "")
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/game_vc.toml" _sma4_vc_identity
    REGEX "^sha1 = \"[0-9a-f]+\"")
if(_sma4_vc_identity)
    list(GET _sma4_vc_identity 0 _sma4_vc_identity)
    string(REGEX REPLACE "^sha1 = \"([0-9a-f]+)\"" "\\1"
        SMA4_VC_ROM_SHA1 "${_sma4_vc_identity}")
endif()
file(GLOB _sma4_vc_generated
    "${CMAKE_CURRENT_SOURCE_DIR}/generated_vc/recompiled_*.cpp")
string(LENGTH "${SMA4_VC_ROM_SHA1}" _sma4_vc_sha1_len)
if(NOT _sma4_vc_sha1_len EQUAL 40)
    message(STATUS "VC Edition: game_vc.toml is unpinned "
        "(run tools/vc_edition.py pin); SuperMarioAdvance4VCRecomp not configured")
elseif(NOT _sma4_vc_generated)
    message(STATUS "VC Edition: generated_vc/ is empty "
        "(run tools/regen.ps1 -Variant vc); SuperMarioAdvance4VCRecomp not configured")
else()
    sma4_add_game(SuperMarioAdvance4VCRecomp generated_vc game_vc.toml 19884
        "SuperMarioAdvance4Recomp (VC Edition)")
    target_compile_definitions(SuperMarioAdvance4VCRecomp PRIVATE
        SMA4_VC_EDITION=1
        SMA4_VC_ROM_SHA1="${SMA4_VC_ROM_SHA1}")
    list(APPEND SMA4_GAME_TARGETS SuperMarioAdvance4VCRecomp)
    message(STATUS "VC Edition: SuperMarioAdvance4VCRecomp (${SMA4_VC_ROM_SHA1})")
endif()

set_property(SOURCE src/main.cpp APPEND PROPERTY COMPILE_DEFINITIONS
    GBARECOMP_GAME_VERSION="${GAME_BUILD_VERSION}")

enable_testing()
add_executable(sma4_view_tests
    tests/extended_view_test.cpp
    src/smb3_extended_view.cpp
    src/smb3_entity_view.cpp
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
    foreach(game_target IN LISTS SMA4_GAME_TARGETS)
        target_link_libraries(${game_target} PRIVATE game_launcher_ui)
    endforeach()
    set_property(SOURCE src/main.cpp APPEND PROPERTY
        COMPILE_DEFINITIONS GBAGAME_RECOMP_UI)
endif()
