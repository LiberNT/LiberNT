
include_directories(${REACTOS_SOURCE_DIR}/sdk/lib/3rdparty/lzma)

add_library(symz
    symz.c
    ${REACTOS_SOURCE_DIR}/sdk/lib/3rdparty/lzma/LzmaDec.c)
target_compile_definitions(symz PRIVATE _7ZIP_ST)
add_dependencies(symz psdk xdk)
