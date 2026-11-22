# ==============================================================================
# kui_media_audio — Standalone DLL plugin for <audio> playback (miniaudio)
#
# This is a *media backend* plugin (kui_media_* protocol): it supplies a decode
# backend to KUI's built-in <audio> element rather than registering a new DOM
# element the way a kuiext_* extension does. See ../README.md for the split.
#
# Build standalone (nothing from the KUI main build is required):
#   cmake -B build -DCMAKE_BUILD_TYPE=Release
#   cmake --build build --config Release
#
# It links nothing from KUI — only <kui/media/kui_media_api.h> is consumed, and
# the host resolves the exported C entry points via LoadLibrary at runtime.
# ==============================================================================
cmake_minimum_required(VERSION 3.20)
project(kui_media_audio LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# --- KUI headers (kui_media_api.h only — no library linkage) ---
# Prefer a packaged SDK so this directory still builds after being copied out
# of the repo (the SDK ships it under <SDK>/extensions/); fall back to the
# in-repo include/ when building from a KUI checkout. Either can be overridden
# with -DKUI_INCLUDE_DIR=<path>.
if(NOT KUI_INCLUDE_DIR)
    find_package(KuiC CONFIG QUIET)
    if(NOT TARGET KuiC::MediaAPI)
        find_package(Kui QUIET)
    endif()
    if(TARGET KuiC::MediaAPI)
        set(_KUI_MEDIA_API_TARGET KuiC::MediaAPI)
    elseif(Kui_FOUND)
        set(KUI_INCLUDE_DIR "${Kui_INCLUDE_DIRS}")
    elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../include/kui/media/kui_media_api.h")
        set(KUI_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../include")
    endif()
endif()
if(NOT _KUI_MEDIA_API_TARGET AND
   NOT EXISTS "${KUI_INCLUDE_DIR}/kui/media/kui_media_api.h")
    message(FATAL_ERROR
        "kui/media/kui_media_api.h not found. Point CMake at a C SDK with "
        "-DKuiC_DIR=<SDK>/cmake, a native SDK with -DKui_DIR=<SDK>/cmake, "
        "or pass -DKUI_INCLUDE_DIR=<include dir>.")
endif()

# --- miniaudio (header-only, MIT-0) ---
# The vendored directory carries its version (`third_party/miniaudio-0.11.25/`),
# so glob for the header instead of hardcoding a directory name — a vendor bump
# then needs no CMake edit. Consumers outside the repo supply their own copy via
# -DMINIAUDIO_DIR=<dir containing miniaudio.h>.
if(NOT MINIAUDIO_DIR)
    file(GLOB _ma_headers
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/miniaudio*/miniaudio.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../third_party/miniaudio*/miniaudio.h")
    if(_ma_headers)
        list(SORT _ma_headers)
        list(REVERSE _ma_headers)   # newest vendored version wins
        list(GET _ma_headers 0 _ma_first)
        get_filename_component(MINIAUDIO_DIR "${_ma_first}" DIRECTORY)
    endif()
endif()
if(NOT EXISTS "${MINIAUDIO_DIR}/miniaudio.h")
    message(FATAL_ERROR
        "miniaudio.h not found.\n"
        "  Fetch it with:  python ${CMAKE_CURRENT_SOURCE_DIR}/../deps/fetch_deps.py --extension kui_media_audio\n"
        "  Or download it yourself (3.9 MB, single header) and pass -DMINIAUDIO_DIR=<dir containing miniaudio.h>.\n"
        "  Addresses, versions and manual steps: extensions/deps/README.md")
endif()
message(STATUS "kui_media_audio: miniaudio = ${MINIAUDIO_DIR}")

add_library(kui_media_audio SHARED
    audio_plugin.c
)

target_compile_definitions(kui_media_audio PRIVATE
    KUI_MEDIA_BUILDING_DLL=1
)

target_include_directories(kui_media_audio PRIVATE
    ${MINIAUDIO_DIR}       # miniaudio.h
)

if(_KUI_MEDIA_API_TARGET)
    target_link_libraries(kui_media_audio PRIVATE ${_KUI_MEDIA_API_TARGET})
else()
    target_include_directories(kui_media_audio PRIVATE
        ${KUI_INCLUDE_DIR} # kui/media/kui_media_api.h
    )
endif()

# No binary library linkage is needed — both authoring target and miniaudio
# are header-only.

# Windows: suppress common warnings for miniaudio
if(MSVC)
    target_compile_options(kui_media_audio PRIVATE /utf-8 /W3 /wd4244 /wd4267 /wd4996)
endif()

# Convenience for the (unusual) case of building this as a subdirectory of a
# KUI checkout: drop the DLL next to html_loader so it is picked up on load.
# The top-level CMakeLists does NOT add_subdirectory this plugin, so under a
# normal standalone build the branch is simply inactive.
if(TARGET kui_html_loader)
    add_custom_command(TARGET kui_media_audio POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:kui_media_audio>
            $<TARGET_FILE_DIR:kui_html_loader>
        COMMENT "Copying kui_media_audio.dll to html_loader output"
    )
endif()
