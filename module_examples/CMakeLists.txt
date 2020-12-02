cmake_minimum_required(VERSION 2.8 FATAL_ERROR)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

# Enable GNU glibc extentions.
add_definitions("-D_GNU_SOURCE")

set(CMAKE_REQUIRED_DEFINITIONS "-D_GNU_SOURCE")

set(select_module_example_sources
    select_module_example.c)

set(h2o_module_example_sources
    h2o_module_example.c)

include_directories(${LIBEV_INCLUDE_DIR})
include_directories(${LUAJIT_INCLUDE_DIRS})

add_library(select_module_example SHARED ${select_module_example_sources})
set_target_properties(select_module_example PROPERTIES PREFIX "")

add_library(h2o_module_example SHARED ${h2o_module_example_sources})
target_link_libraries(h2o_module_example h2o uv)
set_target_properties(h2o_module_example PROPERTIES PREFIX "")