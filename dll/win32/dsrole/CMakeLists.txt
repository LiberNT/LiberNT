
spec2def(dsrole.dll dsrole.spec ADD_IMPORTLIB)

list(APPEND SOURCE
    dsrole_main.c
    ${CMAKE_CURRENT_BINARY_DIR}/dsrole.def)

add_library(dsrole MODULE ${SOURCE})
set_module_type(dsrole win32dll ENTRYPOINT DllMain 12)
add_importlibs(dsrole netapi32 msvcrt kernel32 ntdll)
add_cd_file(TARGET dsrole DESTINATION reactos/system32 FOR all)
