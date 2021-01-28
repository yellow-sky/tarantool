include_directories(${LUAJIT_INCLUDE_DIRS})
add_compile_flags("C;CXX" "-Wall -Werror")

build_module(h2o h2o.c h2o_module.h)
target_link_libraries(h2o /home/void/work/h2o/libh2o-evloop.a /home/void/work/xtm/libxtm_api_static.a)

build_module(sample_site sample_site.c h2o_module.h)
target_link_libraries(sample_site h2o)

