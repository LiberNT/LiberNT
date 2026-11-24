
add_executable(whoami_apitest groups.c testlist.c)
set_module_type(whoami_apitest win32cui)
add_importlibs(whoami_apitest advapi32 msvcrt kernel32)
add_rostests_file(TARGET whoami_apitest)
