
spec2def(samcli.dll samcli.spec ADD_IMPORTLIB)

list(APPEND SOURCE
    samcli_main.c
    ${CMAKE_CURRENT_BINARY_DIR}/samcli.def)

add_library(samcli MODULE ${SOURCE})
set_module_type(samcli win32dll ENTRYPOINT DllMain 12)
add_importlibs(samcli netapi32 msvcrt kernel32 ntdll)
add_cd_file(TARGET samcli DESTINATION reactos/system32 FOR all)
