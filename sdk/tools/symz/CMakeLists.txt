
add_host_tool(symz
    symz.c
    ${REACTOS_SOURCE_DIR}/sdk/lib/3rdparty/lzma/LzmaEnc.c
    ${REACTOS_SOURCE_DIR}/sdk/lib/3rdparty/lzma/LzFind.c)
target_include_directories(symz PRIVATE
    ${REACTOS_SOURCE_DIR}/sdk/lib/3rdparty/lzma
    ${REACTOS_SOURCE_DIR}/sdk/include/reactos)
target_compile_definitions(symz PRIVATE _7ZIP_ST)
target_link_libraries(symz PRIVATE host_includes)
