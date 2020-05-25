include_directories(${MSGPUCK_INCLUDE_DIRS})

build_module(function1 function1.c)
add_library(function1dep STATIC function1dep.c)

target_link_libraries(function1 function1dep)
build_module(reload1 reload1.c)
build_module(reload2 reload2.c)
build_module(tuple_bench tuple_bench.c)
