
list(APPEND SOURCE
    common/constants.c
    common/context.c
    common/dictionary.c
    common/platform.c
    common/shared_dictionary.c
    common/transform.c
    dec/bit_reader.c
    dec/decode.c
    dec/huffman.c
    dec/state.c)

add_library(brotlidec STATIC ${SOURCE})
target_include_directories(brotlidec PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_definitions(brotlidec PRIVATE _DLL __USE_CRTIMP)
target_compile_options(brotlidec PRIVATE -w)
add_dependencies(brotlidec psdk)
