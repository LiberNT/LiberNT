
add_executable(wlantest wlantest.c)
set_module_type(wlantest win32cui UNICODE)
add_importlibs(wlantest wlanapi ws2_32 msvcrt kernel32)
add_cd_file(TARGET wlantest DESTINATION reactos/system32 FOR all)
