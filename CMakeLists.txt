cmake_minimum_required(VERSION 3.18)
project(ntrc_dyndxt)

set_property(GLOBAL PROPERTY TARGET_SUPPORTS_SHARED_LIBS TRUE)
set(CMAKE_SHARED_LIBRARY_SUFFIX ".dll")
set(CMAKE_SHARED_LIBRARY_SUFFIX_C ".dll")
set(CMAKE_SHARED_LIBRARY_SUFFIX_CXX ".dll")

set(CMAKE_VERBOSE_MAKEFILE ON CACHE BOOL "ON")

set(CMAKE_CXX_STANDARD 17)
include(CMakePushCheckState)
set(_CMAKE_PROCESSING_LANGUAGE "C")
include(CheckSymbolExists)
include (ExternalProject)
include(FindPkgConfig)

set(CMAKE_C_CREATE_SHARED_LIBRARY "${NXDK_DIR}/bin/${TOOLCHAIN_PREFIX}-link <CMAKE_C_LINK_FLAGS> <LINK_FLAGS> <OBJECTS> -out:<TARGET> <LINK_LIBRARIES>")
set(CMAKE_CXX_CREATE_SHARED_LIBRARY "${NXDK_DIR}/bin/${TOOLCHAIN_PREFIX}-link <CMAKE_CXX_LINK_FLAGS> <LINK_FLAGS> <OBJECTS> -out:<TARGET> <LINK_LIBRARIES>")

add_subdirectory(devhost)

# The tests are built using the host toolchain rather than the target (nxdk).
ExternalProject_Add(
        ntrc_dyndxt_tests
        PREFIX ntrc_dyndxt_tests
        SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/test
        BINARY_DIR ntrc_dyndxt_tests
        INSTALL_COMMAND ""
        EXCLUDE_FROM_ALL FALSE
        BUILD_ALWAYS TRUE
)

# Import the dynamic DXT loader, which provides some shared functions.
ExternalProject_Add(
        dyndxt_loader
        PREFIX ddxt
        GIT_REPOSITORY https://github.com/abaire/nxdk_dyndxt.git
        GIT_TAG origin/main
        CMAKE_ARGS -DCMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE} -DCMAKE_INSTALL_PREFIX=<INSTALL_DIR>
        BUILD_COMMAND ""
        INSTALL_COMMAND
            cmake --build . --target install --config Release
        BUILD_BYPRODUCTS
        ddxt/lib/xbdm.lib
        ddxt/lib/libdynamic_dxt_loader.lib
        ddxt/include/command_processor_util.h
        ddxt/include/module_registry.h
        ddxt/include/nxdk_dxt_dll_main.h
        ddxt/include/xbdm.h
        ddxt/include/xbdm_err.h
)
# The directory of header files for methods exported by the dynamic DXT loader.
ExternalProject_Get_Property(dyndxt_loader INSTALL_DIR)
set(dyndxt_lib_dir ${INSTALL_DIR}/lib)
set(dyndxt_include_dir ${INSTALL_DIR}/include)

# Pre-create the directory to pass CMake generator check.
file (MAKE_DIRECTORY ${dyndxt_include_dir})

# Import library containing various XBDM DLL exports.
add_library(xbdm_lib SHARED IMPORTED)
set_target_properties(
        xbdm_lib
        PROPERTIES
        IMPORTED_LOCATION ${dyndxt_lib_dir}/xbdm.lib
        INTERFACE_INCLUDE_DIRECTORIES ${dyndxt_include_dir}
)
add_dependencies(xbdm_lib dyndxt_loader)

# Import library containing Dynamic DXT DLL exports.
add_library(dynamic_dxt_loader_lib SHARED IMPORTED)
set_target_properties(
        dynamic_dxt_loader_lib
        PROPERTIES
        IMPORTED_LOCATION ${dyndxt_lib_dir}/libdynamic_dxt_loader.lib
        INTERFACE_INCLUDE_DIRECTORIES ${dyndxt_include_dir}
)
add_dependencies(dynamic_dxt_loader_lib dyndxt_loader)

set(LIB_TARGET tracelib)
add_library(
        "${LIB_TARGET}"
        STATIC
        src/util/circular_buffer.c
        src/util/circular_buffer.h
        src/util/circular_buffer_impl.h
        src/tracelib/exchange_dword.c
        src/tracelib/exchange_dword.h
        src/tracelib/kick_fifo.c
        src/tracelib/kick_fifo.h
        src/tracelib/ntrc_dyndxt.h
        src/tracelib/pgraph_command_callbacks.c
        src/tracelib/pgraph_command_callbacks.h
        src/tracelib/pushbuffer_command.c
        src/tracelib/pushbuffer_command.h
        src/tracelib/register_defs.h
        src/tracelib/tracer_state_machine.c
        src/tracelib/tracer_state_machine.h
        src/tracelib/xbox_helper.c
        src/tracelib/xbox_helper.h
        third_party/xemu/hw/xbox/nv2a/nv2a_regs.h
        "${dyndxt_include_dir}/xbdm.h"
        "${dyndxt_include_dir}/xbdm_err.h"
)
target_include_directories(
        "${LIB_TARGET}"
        PUBLIC
        src
        third_party
        "${NXDK_DIR}"
        "${dyndxt_include_dir}"
)
target_link_options(
        "${LIB_TARGET}"
        PUBLIC
        /machine:i386
)
target_link_libraries(
        "${LIB_TARGET}"
        PUBLIC
        dynamic_dxt_loader_lib
        xbdm_lib
)

if(CMAKE_BUILD_TYPE MATCHES Debug)
    target_compile_options(
            "${LIB_TARGET}"
            PRIVATE
            -g
            -O0
            -gdwarf-4
            -Wall
            -fstandalone-debug
    )
    target_link_options("${LIB_TARGET}" PRIVATE "/debug:full")
else()
    target_compile_options(
            "${LIB_TARGET}"
            PRIVATE
            -O3
            -Wall
    )
    target_link_options("${LIB_TARGET}" PRIVATE "/debug:none")
endif()


set(TARGET ntrc_dyndxt)
add_library(
        "${TARGET}"
        SHARED
        src/cmd_attach.c
        src/cmd_attach.h
        src/cmd_detach.c
        src/cmd_detach.h
        src/cmd_discard_until_flip.c
        src/cmd_discard_until_flip.h
        src/cmd_get_dma_addrs.c
        src/cmd_get_dma_addrs.h
        src/cmd_get_state.c
        src/cmd_get_state.h
        src/cmd_hello.c
        src/cmd_hello.h
        src/cmd_read_aux.c
        src/cmd_read_aux.h
        src/cmd_read_pgraph.c
        src/cmd_read_pgraph.h
        src/cmd_trace_frame.c
        src/cmd_trace_frame.h
        src/cmd_wait_for_stable_push_buffer_state.c
        src/cmd_wait_for_stable_push_buffer_state.h
        src/dxtmain.c
        src/dxtmain.h
        src/xbdm_util.c
        src/xbdm_util.h
        "${dyndxt_include_dir}/command_processor_util.h"
        "${dyndxt_include_dir}/nxdk_dxt_dll_main.h"
)

target_include_directories(
        "${TARGET}"
        PRIVATE
        third_party
        "${NXDK_DIR}"
        "${dyndxt_include_dir}"
)

target_link_options(
        "${TARGET}"
        PUBLIC
        /machine:i386
        /entry:DXTMainCRTStartup@0
        /dynamicbase
        /fixed:no
)
target_link_libraries(
        "${TARGET}"
        PRIVATE
        "${LIB_TARGET}"
)

if(CMAKE_BUILD_TYPE MATCHES Debug)
    target_compile_options(
            "${TARGET}"
            PRIVATE
            -g
            -O0
            -gdwarf-4
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


install(
        FILES
        ${CMAKE_CURRENT_BINARY_DIR}/lib${TARGET}.dll
        DESTINATION
        lib
)

install(
        FILES
        src/tracelib/ntrc_dyndxt.h
        DESTINATION
        include
)
