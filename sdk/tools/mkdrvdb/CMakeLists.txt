
add_host_tool(mkdrvdb mkdrvdb.c)
if(NOT MSVC)
    target_compile_options(mkdrvdb PRIVATE "-fshort-wchar")
endif()

target_link_libraries(mkdrvdb PRIVATE host_includes inflibhost unicode)
