
add_executable(netsh_apitest commandline.c testlist.c)
set_module_type(netsh_apitest win32cui)
add_importlibs(netsh_apitest msvcrt kernel32)
add_rostests_file(TARGET netsh_apitest)
