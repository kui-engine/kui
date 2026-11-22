# ==============================================================================
# kui_c — KUI C ABI layer
#
# Produces kui_c.dll, which exports a pure C ABI and consumes kui.dll's C++ ABI
# internally. That split is the whole point: kui_c.dll and kui.dll must be built
# with the same compiler and CRT (both are shipped by us, so the constraint never
# reaches the consumer), while a consumer of kui_c.dll is free to use any
# compiler, any CRT, any language with an FFI.
#
# Build in tree (from the KUI repo, where the `kui` target already exists), or
# standalone against a packaged SDK:
#   cmake -B build -DKui_DIR=<SDK>/cmake
#   cmake --build build --config Release
# ==============================================================================
cmake_minimum_required(VERSION 3.20)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(kui_c LANGUAGES CXX C)
endif()

# The implementation uses std::string_view to pass caller bytes through without
# copying. Consumers of kui_c.h need no particular standard -- it is plain C.
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(TARGET kui)
    # In-tree: link the live target so a core change rebuilds this layer too.
    set(_KUI_C_LINK_TARGET kui)
    message(STATUS "kui_c: linking in-tree target 'kui'")
else()
    find_package(Kui REQUIRED)
    set(_KUI_C_LINK_TARGET Kui::Kui)
    message(STATUS "kui_c: linking packaged SDK ${Kui_VERSION}")
endif()

add_library(kui_c SHARED
    src/kui_c_core.cpp
    src/kui_c_document.cpp
    src/kui_c_element.cpp
    src/kui_c_event.cpp
    src/kui_c_style.cpp
    src/kui_c_platform.cpp
    src/kui_c_devtools.cpp
    src/kui_c_full.cpp
    src/kui_c_common.cpp
    src/kui_c_desktop.cpp
    src/kui_c_framework.cpp
    src/kui_c_widgets.cpp
    src/kui_c_components.cpp
    src/kui_c_diagnostics.cpp
)

target_include_directories(kui_c
    PUBLIC  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_compile_definitions(kui_c PRIVATE KUI_C_BUILDING_LIBRARY)
target_link_libraries(kui_c PRIVATE ${_KUI_C_LINK_TARGET})

if(MSVC)
    target_compile_options(kui_c PRIVATE /utf-8 /W4)
    # A Windows GA package always has matching symbols for both Release and
    # Debug. /DEBUG:FULL is intentionally local to this bridge DLL; consumers
    # do not inherit it through the C target.
    target_link_options(kui_c PRIVATE /DEBUG:FULL)
    set_target_properties(kui_c PROPERTIES PDB_NAME "kui_c")
else()
    # Only KUI_C_API functions (visibility("default")) leave the shared object,
    # the same set a Windows DLL exports; check_c_abi.py compares it exactly.
    set_target_properties(kui_c PROPERTIES
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN ON)
    target_compile_options(kui_c PRIVATE -Wall -Wextra)
endif()

# The example and test sources are deliberately pure ASCII so that they compile
# under any code page with any of MSVC/gcc/clang -- they are reference material
# for third parties. /utf-8 below is belt-and-braces for future edits: on a
# Chinese Windows install MSVC defaults to code page 936 and a single non-ASCII
# byte in a comment raises C4819 (which is how the em dashes originally here
# were caught).

# --- Full Profile examples ---------------------------------------------------
# These are real pure-C entry points, not C++ sources compiled as C. Keep the
# list explicit and count-gated so a release cannot silently fall back to the
# original two Core Profile demos.
set(_kui_c_example_sources
    hello_world.c
    window_demo.c
    lua_dom_demo.c
    http_client_demo.c
    platform_services_demo.c
    custom_element_demo.c
    media_lottie_demo.c
    extension_3d_demo.c
    canvas_webgl_demo.c
    pdf_export_demo.c
    devtools_demo.c
)
set(KUI_C_EXAMPLE_TARGETS "")
foreach(_source IN LISTS _kui_c_example_sources)
    get_filename_component(_name "${_source}" NAME_WE)
    set(_target "kui_c_${_name}")
    add_executable(${_target} "examples/${_source}")
    target_link_libraries(${_target} PRIVATE kui_c)
    list(APPEND KUI_C_EXAMPLE_TARGETS ${_target})
endforeach()
list(LENGTH KUI_C_EXAMPLE_TARGETS _kui_c_example_count)
if(NOT _kui_c_example_count EQUAL 11)
    message(FATAL_ERROR
        "KUI C Full Profile expects exactly 11 pure-C examples, got ${_kui_c_example_count}")
endif()
message(STATUS "kui_c: ${_kui_c_example_count} pure-C Full Profile examples")

# --- Tests: also written in C, for the same reason ---
add_executable(test_kui_c_abi tests/test_kui_c_abi.c)
target_link_libraries(test_kui_c_abi PRIVATE kui_c)
add_executable(test_kui_c_full tests/test_kui_c_full.c)
target_link_libraries(test_kui_c_full PRIVATE kui_c)
# Cross-thread hand-off and the platform lifecycle around it; its own process
# because it runs init -> shutdown and checks posts are refused on both sides.
add_executable(test_kui_c_threading tests/test_kui_c_threading.c)
target_link_libraries(test_kui_c_threading PRIVATE kui_c)
if(NOT WIN32)
    find_package(Threads REQUIRED)
    target_link_libraries(test_kui_c_threading PRIVATE Threads::Threads)
endif()
# Section 9 (window / DOM / platform additions); creates one real window.
add_executable(test_kui_c_common tests/test_kui_c_common.c)
target_link_libraries(test_kui_c_common PRIVATE kui_c)
# Section 10 (menu bar / tray / shortcuts / toasts / screenshot); one real window.
add_executable(test_kui_c_desktop tests/test_kui_c_desktop.c)
target_link_libraries(test_kui_c_desktop PRIVATE kui_c)

add_executable(test_kui_c_framework tests/test_kui_c_framework.c)
target_link_libraries(test_kui_c_framework PRIVATE kui_c)

add_executable(test_kui_c_widgets tests/test_kui_c_widgets.c)
target_link_libraries(test_kui_c_widgets PRIVATE kui_c)

# Both executables need kui.dll and kui_c.dll beside them.
foreach(_tgt IN LISTS KUI_C_EXAMPLE_TARGETS)
    add_custom_command(TARGET ${_tgt} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:kui_c> $<TARGET_FILE_DIR:${_tgt}>
        VERBATIM)
    if(TARGET kui)
        add_custom_command(TARGET ${_tgt} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    $<TARGET_FILE:kui> $<TARGET_FILE_DIR:${_tgt}>
            VERBATIM)
    endif()
endforeach()

foreach(_tgt test_kui_c_abi test_kui_c_full test_kui_c_threading test_kui_c_common test_kui_c_desktop
             test_kui_c_framework test_kui_c_widgets)
    add_custom_command(TARGET ${_tgt} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:kui_c> $<TARGET_FILE_DIR:${_tgt}>
        VERBATIM)
    if(TARGET kui)
        add_custom_command(TARGET ${_tgt} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    $<TARGET_FILE:kui> $<TARGET_FILE_DIR:${_tgt}>
            VERBATIM)
    endif()
endforeach()

if(MSVC)
    foreach(_tgt IN LISTS KUI_C_EXAMPLE_TARGETS)
        target_compile_options(${_tgt} PRIVATE /utf-8 /W4)
    endforeach()
    target_compile_options(test_kui_c_abi    PRIVATE /utf-8)
    target_compile_options(test_kui_c_full   PRIVATE /utf-8)
    target_compile_options(test_kui_c_threading PRIVATE /utf-8)
    target_compile_options(test_kui_c_common PRIVATE /utf-8)
    target_compile_options(test_kui_c_desktop PRIVATE /utf-8)
    target_compile_options(test_kui_c_framework PRIVATE /utf-8)
    target_compile_options(test_kui_c_widgets PRIVATE /utf-8)
endif()

enable_testing()
add_test(NAME kui_c_abi COMMAND test_kui_c_abi)
# kui_c_full loads the hello extension from "." -- run it where the extension is deployed.
add_test(NAME kui_c_full COMMAND test_kui_c_full
         WORKING_DIRECTORY $<TARGET_FILE_DIR:test_kui_c_full>)
add_test(NAME kui_c_threading COMMAND test_kui_c_threading)
add_test(NAME kui_c_common COMMAND test_kui_c_common)
add_test(NAME kui_c_desktop COMMAND test_kui_c_desktop)
add_test(NAME kui_c_framework COMMAND test_kui_c_framework)
add_test(NAME kui_c_widgets COMMAND test_kui_c_widgets)
