
list(APPEND SOURCE
    blocksort.c
    bzlib.c
    compress.c
    crctable.c
    decompress.c
    huffman.c
    randtable.c)

add_library(bzip2 ${SOURCE})
target_include_directories(bzip2 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(bzip2 PUBLIC BZ_NO_STDIO)
if(NOT MSVC)
    target_compile_options(bzip2 PRIVATE -w)
endif()
add_dependencies(bzip2 psdk)
