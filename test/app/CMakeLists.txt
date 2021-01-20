build_module(loaderslib loaderslib.c)
build_module(xtmlib xtmlib.c)
target_link_libraries(xtmlib xtm_api_static)
