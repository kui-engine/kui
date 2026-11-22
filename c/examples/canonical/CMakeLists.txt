cmake_minimum_required(VERSION 3.20)
project(kui_c_canonical_examples LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

find_package(KuiC 3 CONFIG REQUIRED)

# Executables resolve pages and assets beside themselves, and the assets below are copied to
# <build>/<config>/assets. Multi-config generators (Visual Studio) already put executables in
# <build>/<config>; single-config ones (Ninja / Makefiles on Linux and macOS) would put them in
# <build>, away from their assets.
if(NOT CMAKE_CONFIGURATION_TYPES AND NOT WIN32)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIG>")
endif()

add_library(kui_c_example_runtime STATIC
    runtime/kui_c_example_runtime.c
    runtime/kui_c_example_runtime.h)
target_include_directories(kui_c_example_runtime PUBLIC
    "${CMAKE_CURRENT_LIST_DIR}/runtime")
target_link_libraries(kui_c_example_runtime PUBLIC KuiC::C)
if(MSVC)
    target_compile_options(kui_c_example_runtime PRIVATE /utf-8 /W4)
endif()

function(kui_c_add_port target source)
    add_executable(${target} "${source}" ${ARGN})
    target_link_libraries(${target} PRIVATE kui_c_example_runtime)
    kui_c_copy_runtime_dlls(${target})
    if(MSVC)
        target_compile_options(${target} PRIVATE /utf-8 /W4)
    endif()
    set_target_properties(${target} PROPERTIES FOLDER "KUI C Canonical Examples")
    list(APPEND KUI_C_PORT_TARGETS ${target})
    set(KUI_C_PORT_TARGETS "${KUI_C_PORT_TARGETS}" PARENT_SCOPE)
endfunction()

include("${CMAKE_CURRENT_LIST_DIR}/manifest.cmake")

# All examples resolve pages and domain assets beside the executable. Copy once
# per configuration rather than attaching the same large copy command to every example.
add_custom_target(kui_c_example_assets ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_LIST_DIR}/assets"
            "${CMAKE_BINARY_DIR}/$<CONFIG>/assets"
    COMMENT "[KUI C] staging canonical example assets")

# In the repository, large/binary canonical assets remain at their existing
# ownership locations. A released C SDK materializes the same directories under
# examples/canonical/assets, so these extra copies simply become no-ops there.
set(_kui_c_repo_root "${CMAKE_CURRENT_LIST_DIR}/../..")
if(EXISTS "${_kui_c_repo_root}/examples/html_loader/samples")
    add_custom_command(TARGET kui_c_example_assets POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${_kui_c_repo_root}/examples/html_loader/samples"
                "${CMAKE_BINARY_DIR}/$<CONFIG>/assets/native_samples")
endif()
if(EXISTS "${_kui_c_repo_root}/tests/script/html")
    # kui_script_example looks for assets/script_html next to the executable (a released C SDK carries it
    # under examples/canonical/assets/script_html, so this copy is a no-op there).
    add_custom_command(TARGET kui_c_example_assets POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${_kui_c_repo_root}/tests/script/html"
                "${CMAKE_BINARY_DIR}/$<CONFIG>/assets/script_html")
endif()
if(EXISTS "${_kui_c_repo_root}/examples/native/animation/assets/pulse.json")
    add_custom_command(TARGET kui_c_example_assets POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory
                "${CMAKE_BINARY_DIR}/$<CONFIG>/assets/native_animation"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${_kui_c_repo_root}/examples/native/animation/assets/pulse.json"
                "${CMAKE_BINARY_DIR}/$<CONFIG>/assets/native_animation/pulse.json")
endif()
if(EXISTS "${_kui_c_repo_root}/resources/audio/sample.mp3")
    add_custom_command(TARGET kui_c_example_assets POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory
                "${CMAKE_BINARY_DIR}/$<CONFIG>/assets/native_media"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${_kui_c_repo_root}/resources/audio/sample.mp3"
                "${CMAKE_BINARY_DIR}/$<CONFIG>/assets/native_media/sample.mp3"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${_kui_c_repo_root}/resources/video/sample.mp4"
                "${CMAKE_BINARY_DIR}/$<CONFIG>/assets/native_media/sample.mp4")
endif()
if(EXISTS "${_kui_c_repo_root}/resources/3d models/stylized_tank.glb")
    add_custom_command(TARGET kui_c_example_assets POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory
                "${CMAKE_BINARY_DIR}/$<CONFIG>/assets/native_3d"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${_kui_c_repo_root}/resources/3d models/stylized_tank.glb"
                "${CMAKE_BINARY_DIR}/$<CONFIG>/assets/native_3d/stylized_tank.glb")
endif()
if(EXISTS "${_kui_c_repo_root}/examples/font_awesome_showcase/css")
    add_custom_command(TARGET kui_c_example_assets POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${_kui_c_repo_root}/examples/font_awesome_showcase/css"
                "${CMAKE_BINARY_DIR}/$<CONFIG>/assets/pages/css"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${_kui_c_repo_root}/examples/font_awesome_showcase/webfonts"
                "${CMAKE_BINARY_DIR}/$<CONFIG>/assets/pages/webfonts")
endif()
add_dependencies(kui_c_example_assets ${KUI_C_PORT_TARGETS})
