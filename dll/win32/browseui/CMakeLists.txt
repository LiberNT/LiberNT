
spec2def(browseui.dll browseui.spec)

list(APPEND SOURCE
    version.rc
    ${CMAKE_CURRENT_BINARY_DIR}/browseui.def)

add_library(browseui MODULE ${SOURCE})
set_module_type(browseui win32dll ENTRYPOINT 0)
add_dependencies(browseui xdk)
add_importlibs(browseui kernel32 ntdll)
add_cd_file(TARGET browseui DESTINATION reactos/system32 FOR all)
