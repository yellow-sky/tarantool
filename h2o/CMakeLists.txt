include_directories(${LUAJIT_INCLUDE_DIRS})
add_compile_flags("C;CXX" "-Wall -Werror")
build_module(h2o h2o.c)
target_link_libraries(h2o /home/void/work/h2o/libh2o-evloop.a /home/void/work/xtm/libxtm_api_static.a)

