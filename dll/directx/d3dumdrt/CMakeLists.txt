spec2def(d3dumdrt.dll d3dumdrt.spec)

add_library(d3dumdrt MODULE
    d3dumdrt.c
    ${CMAKE_CURRENT_BINARY_DIR}/d3dumdrt.def)

set_module_type(d3dumdrt win32dll ENTRYPOINT DllMain 12)
target_link_libraries(d3dumdrt pseh)
add_importlibs(d3dumdrt msvcrt kernel32 ntdll)
add_dependencies(d3dumdrt psdk)
add_cd_file(TARGET d3dumdrt DESTINATION reactos/system32 FOR all)
