set(lib_sources crypto.c)

set_source_files_compile_flags(${lib_sources})
add_library(crypto STATIC ${lib_sources})
if (BUILD_STATIC)
    add_dependencies(crypto openssl)
endif()
target_link_libraries(crypto ${OPENSSL_LIBRARIES} core)
