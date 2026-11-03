
add_executable(rundll32_apitest wow64.c testlist.c)
set_module_type(rundll32_apitest win32cui)
add_importlibs(rundll32_apitest advapi32 msvcrt kernel32)
add_rostests_file(TARGET rundll32_apitest)
