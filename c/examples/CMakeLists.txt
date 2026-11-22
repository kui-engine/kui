cmake_minimum_required(VERSION 3.20)
project(kui_c_full_profile_examples LANGUAGES C)

find_package(KuiC CONFIG REQUIRED)

set(_kui_c_examples
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

set(KUI_C_PACKAGE_EXAMPLE_TARGETS "")
foreach(_source IN LISTS _kui_c_examples)
    get_filename_component(_name "${_source}" NAME_WE)
    set(_target "kui_c_example_${_name}")
    add_executable(${_target} "${_source}")
    target_link_libraries(${_target} PRIVATE KuiC::C)
    kui_c_copy_runtime_dlls(${_target})
    if(MSVC)
        target_compile_options(${_target} PRIVATE /utf-8 /W4)
    endif()
    list(APPEND KUI_C_PACKAGE_EXAMPLE_TARGETS ${_target})
endforeach()

list(LENGTH KUI_C_PACKAGE_EXAMPLE_TARGETS _count)
if(NOT _count EQUAL 11)
    message(FATAL_ERROR "Expected exactly 11 packaged C examples, got ${_count}")
endif()
message(STATUS "KuiC package: ${_count} pure-C Full Profile examples")
