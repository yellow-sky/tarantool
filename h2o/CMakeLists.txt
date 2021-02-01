#set(H2O_USE_LIBUV ON)

include_directories(${LUAJIT_INCLUDE_DIRS})
add_compile_flags("C;CXX" "-Wall -Werror")

build_module(h2o h2o.c h2o_module.h)
target_link_libraries(h2o /home/void/work/xtm/libxtm_api_static.a)

if (H2O_USE_LIBUV)
add_definitions(-DUSE_LIBUV)
target_link_libraries(h2o /home/void/work/libuv/.libs/libuv.a /home/void/work/h2o/libh2o.a)
else (H2O_USE_LIBUV)
target_link_libraries(h2o /home/void/work/h2o/libh2o-evloop.a)
endif (H2O_USE_LIBUV)

build_module(sample_site sample_site.c h2o_module.h)
target_link_libraries(sample_site h2o)

