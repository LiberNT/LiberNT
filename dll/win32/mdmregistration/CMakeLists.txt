spec2def(mdmregistration.dll mdmregistration.spec)

add_library(mdmregistration MODULE
    mdmregistration.c
    ${CMAKE_CURRENT_BINARY_DIR}/mdmregistration.def)

set_module_type(mdmregistration win32dll)
add_importlibs(mdmregistration msvcrt kernel32 ntdll)
add_cd_file(TARGET mdmregistration DESTINATION reactos/system32 FOR all)
