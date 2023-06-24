cmake_minimum_required(VERSION 3.18)
project(devhost)

set(CMAKE_MODULE_PATH
        ${CMAKE_MODULE_PATH}
        "${CMAKE_CURRENT_LIST_DIR}/cmake/modules/"
)

set_property(GLOBAL PROPERTY TARGET_SUPPORTS_SHARED_LIBS TRUE)
set(CMAKE_SHARED_LIBRARY_SUFFIX ".dll")
set(CMAKE_SHARED_LIBRARY_SUFFIX_C ".dll")
set(CMAKE_SHARED_LIBRARY_SUFFIX_CXX ".dll")

set(CMAKE_VERBOSE_MAKEFILE ON CACHE BOOL "ON")

set(CMAKE_CXX_STANDARD 17)
include(CMakePushCheckState)
set(_CMAKE_PROCESSING_LANGUAGE "CXX")
include(CheckSymbolExists)
include(ExternalProject)
include(FindPkgConfig)

set(TARGET devhost)

find_package(NXDK REQUIRED)

add_executable(
        "${TARGET}"
        EXCLUDE_FROM_ALL
        src/debug_output.cpp
        src/debug_output.h
        src/main.cpp
        src/math3d.c
        src/math3d.h
        src/nxdk_ext.h
        src/pbkit_ext.cpp
        src/pbkit_ext.h
        src/renderer.cpp
        src/renderer.h
        src/shaders/vertex_shader_program.cpp
        src/shaders/vertex_shader_program.h
        src/texture_format.cpp
        src/texture_format.h
        src/texture_generator.cpp
        src/texture_generator.h
        src/texture_stage.cpp
        src/texture_stage.h
        src/vertex_buffer.cpp
        src/vertex_buffer.h
        third_party/printf/printf.c
        third_party/printf/printf.h
)
set(EXECUTABLE_BINARY "${CMAKE_CURRENT_BINARY_DIR}/${TARGET}.exe")
target_include_directories(
        "${TARGET}"
        PRIVATE
        src
        third_party
)

if(CMAKE_BUILD_TYPE MATCHES Debug)
    target_compile_options(
            "${TARGET}"
            PRIVATE
            -g
            -gdwarf-4
            -O0
            -Wall
            -fstandalone-debug
    )
    target_link_options("${TARGET}" PRIVATE "/debug:full")
else()
    target_compile_options(
            "${TARGET}"
            PRIVATE
            -O3
            -Wall
    )
    target_link_options("${TARGET}" PRIVATE "/debug:none")
endif()

target_link_libraries(
        "${TARGET}"
        ntrc_dyndxt_static
        NXDK::NXDK
        NXDK::NXDK_CXX
)

# Target to convert the executable into an XBE
set(XBE_STAGING_DIR "${CMAKE_CURRENT_BINARY_DIR}/xbe")
set(XBE_OUTPUT_PATH "${XBE_STAGING_DIR}/default.xbe")

set(CXBE_TOOL_PATH "${NXDK_DIR}/tools/cxbe/cxbe")

add_custom_command(
        OUTPUT "${XBE_OUTPUT_PATH}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${XBE_STAGING_DIR}"
        COMMAND "${CXBE_TOOL_PATH}"
        "-TITLE:NTRCDevHost"
        "-OUT:${XBE_OUTPUT_PATH}"
        "-MODE:DEBUG"
        "${EXECUTABLE_BINARY}"
        DEPENDS "${TARGET}"
)

set(XISO_STAGING_DIR "${CMAKE_CURRENT_BINARY_DIR}/xiso")
set(XISO_OUTPUT_PATH "${XISO_STAGING_DIR}/${TARGET}.iso")
set(EXTRACT_XISO_TOOL_PATH "${NXDK_DIR}/tools/extract-xiso/build/extract-xiso")

add_custom_command(
        OUTPUT "${XISO_OUTPUT_PATH}"
        COMMAND "${EXTRACT_XISO_TOOL_PATH}" -c "${XBE_STAGING_DIR}" "${XISO_OUTPUT_PATH}"
        DEPENDS "${XBE_OUTPUT_PATH}"
)

set(XISO_TARGET devhost_xiso)
add_custom_target("${XISO_TARGET}" DEPENDS "${XISO_OUTPUT_PATH}")
