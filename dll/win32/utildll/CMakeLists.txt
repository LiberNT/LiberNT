
spec2def(utildll.dll utildll.spec)

list(APPEND SOURCE
    utildll.c
    ${CMAKE_CURRENT_BINARY_DIR}/utildll_stubs.c
    ${CMAKE_CURRENT_BINARY_DIR}/utildll.def)

add_library(utildll MODULE ${SOURCE} utildll.rc)
set_module_type(utildll win32dll UNICODE)
add_importlibs(utildll user32 advapi32 msvcrt kernel32 ntdll)
add_cd_file(TARGET utildll DESTINATION reactos/system32 FOR all)
