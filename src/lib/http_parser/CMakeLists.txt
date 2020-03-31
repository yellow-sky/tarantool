add_library(http_parser STATIC http_parser.c)

set(TEST test_http_parser)
add_executable(${TEST} ${TEST}.c)
target_compile_options(${TEST}
            PRIVATE $<$<C_COMPILER_ID:Clang>:-g -O1 -fsanitize=fuzzer>
            )
target_link_libraries(${TEST}
            PRIVATE $<$<C_COMPILER_ID:Clang>:-fsanitize=fuzzer>
            http_parser)
