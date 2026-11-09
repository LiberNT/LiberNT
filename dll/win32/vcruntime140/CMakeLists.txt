spec2def(vcruntime140.dll vcruntime140.spec ADD_IMPORTLIB)

add_library(vcruntime140 SHARED
    dllmain.c
    vcruntime140.rc
    ${CMAKE_CURRENT_BINARY_DIR}/vcruntime140.def)

set_module_type(vcruntime140 win32dll ENTRYPOINT DllMain 12)
add_importlibs(vcruntime140 ucrtbase kernel32 ntdll)
add_dependencies(vcruntime140 psdk)
add_cd_file(TARGET vcruntime140 DESTINATION reactos/system32 FOR all)
