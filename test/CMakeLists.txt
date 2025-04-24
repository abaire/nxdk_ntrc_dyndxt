cmake_minimum_required(VERSION 3.18)
project(ntrc_dyndxt_tests)

set(CMAKE_VERBOSE_MAKEFILE TRUE)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -g -Og")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -Og")

enable_testing()
find_package(
        Boost 1.70
        COMPONENTS
        unit_test_framework
        REQUIRED
)
include_directories("${Boost_INCLUDE_DIR}")

# Tests ----------------------------------------------

set(ntrc_dyndxt_source_directory "${CMAKE_HOME_DIRECTORY}/../src")

# command_processor_util_tests
add_executable(
        circular_buffer_tests
        util/circular_buffer/test_main.cpp
        "${ntrc_dyndxt_source_directory}/util/circular_buffer.c"
        "${ntrc_dyndxt_source_directory}/util/circular_buffer.h"
        "${ntrc_dyndxt_source_directory}/util/circular_buffer_impl.h"
)
target_include_directories(
        circular_buffer_tests
        PRIVATE
        "${ntrc_dyndxt_source_directory}"
        stub
)
target_link_libraries(
        circular_buffer_tests
        LINK_PRIVATE
        "${Boost_LIBRARIES}"
)
add_test(NAME circular_buffer_tests COMMAND circular_buffer_tests)
