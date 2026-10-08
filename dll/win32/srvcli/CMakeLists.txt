
spec2def(srvcli.dll srvcli.spec ADD_IMPORTLIB)

list(APPEND SOURCE
    srvcli_main.c
    ${CMAKE_CURRENT_BINARY_DIR}/srvcli.def)

add_library(srvcli MODULE ${SOURCE})
set_module_type(srvcli win32dll ENTRYPOINT DllMain 12)
add_importlibs(srvcli netapi32 msvcrt kernel32 ntdll)
add_cd_file(TARGET srvcli DESTINATION reactos/system32 FOR all)
