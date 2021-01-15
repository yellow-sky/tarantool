set(xtm_api_sources
    xtm_api.cc
)

set(on_shutdown_sources
    on_shutdown.c
)

include_directories(${LIBEV_INCLUDE_DIR})

add_library(xtm_api SHARED ${xtm_api_sources})
add_library(on_shutdown STATIC ${on_shutdown_sources})