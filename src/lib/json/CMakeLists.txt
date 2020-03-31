set(lib_sources
    json.c
)

set_source_files_compile_flags(${lib_sources})
add_library(json STATIC ${lib_sources})
target_link_libraries(json misc)

add_executable(test_json test_json.c)
target_compile_options(test_json
            PRIVATE $<$<C_COMPILER_ID:Clang>:-g -O1 -fsanitize=fuzzer>
            )
target_link_libraries(test_json
            PRIVATE $<$<C_COMPILER_ID:Clang>:-fsanitize=fuzzer>
            json)
