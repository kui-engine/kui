# ===========================================================================
# kui_ext_hello — Minimal example extension DLL
# ===========================================================================
cmake_minimum_required(VERSION 3.16)

if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
    project(kui_ext_hello LANGUAGES C)
    # The same source is delivered by both SDKs. Prefer the pure-C package
    # target when available; otherwise a native C++ SDK supplies the protocol
    # header through Kui::Kui. No KUI binary symbol is linked by this plugin.
    find_package(KuiC CONFIG QUIET)
    if(NOT TARGET KuiC::ExtensionAPI)
        find_package(Kui CONFIG QUIET)
    endif()
endif()

add_library(kui_ext_hello SHARED kui_ext_hello.c)

if(TARGET KuiC::ExtensionAPI)
    target_link_libraries(kui_ext_hello PRIVATE KuiC::ExtensionAPI)
elseif(DEFINED KUI_EXAMPLES_ROOT_DIR AND
       EXISTS "${KUI_EXAMPLES_ROOT_DIR}/include/kui/extension/kui_extension_api.h")
    target_include_directories(kui_ext_hello PRIVATE
        "${KUI_EXAMPLES_ROOT_DIR}/include")
elseif(TARGET Kui::Kui)
    target_link_libraries(kui_ext_hello PRIVATE Kui::Kui)
else()
    message(FATAL_ERROR
        "kui_ext_hello needs KuiC::ExtensionAPI, Kui::Kui, or "
        "KUI_EXAMPLES_ROOT_DIR pointing at an SDK root")
endif()

target_compile_definitions(kui_ext_hello PRIVATE KUI_EXT_BUILDING_DLL)
if(MSVC)
    target_compile_options(kui_ext_hello PRIVATE /utf-8 /W4)
endif()

# Output to build/extensions/ directory for easy discovery
set_target_properties(kui_ext_hello PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/extensions"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/extensions"
)
