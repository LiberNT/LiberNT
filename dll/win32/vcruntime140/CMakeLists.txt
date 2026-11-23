spec2def(vcruntime140.dll vcruntime140.spec)

add_library(vcruntime140 MODULE
    vcruntime140.c
    ${CMAKE_CURRENT_BINARY_DIR}/vcruntime140.def)

set_module_type(vcruntime140 win32dll)
add_importlibs(vcruntime140 ucrtbase kernel32 ntdll)
add_cd_file(TARGET vcruntime140 DESTINATION reactos/system32 FOR all)
