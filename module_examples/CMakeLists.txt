cmake_minimum_required(VERSION 2.8 FATAL_ERROR)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

# Enable GNU glibc extentions.
add_definitions("-D_GNU_SOURCE")

set(CMAKE_REQUIRED_DEFINITIONS "-D_GNU_SOURCE")

set(example_sources example.c)

include_directories(${LUAJIT_INCLUDE_DIRS})
include_directories(${PROJECT_SOURCE_DIR}/src/)

add_library(example SHARED ${example_sources})
target_link_libraries(example uv xtm_api)
set_target_properties(example PROPERTIES PREFIX "")
