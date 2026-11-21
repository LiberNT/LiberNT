
spec2def(softgpuum.dll softgpuum.spec)

add_library(softgpuum MODULE
    softgpuum.c
    softgpuum_backend_none.c
    softgpuum.rc
    ${CMAKE_CURRENT_BINARY_DIR}/softgpuum.def)

set_module_type(softgpuum win32dll ENTRYPOINT DllMain 12)
add_importlibs(softgpuum msvcrt kernel32 ntdll)
add_dependencies(softgpuum psdk)
add_cd_file(TARGET softgpuum DESTINATION reactos/system32 FOR all)
