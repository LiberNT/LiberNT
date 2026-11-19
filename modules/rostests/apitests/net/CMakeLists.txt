
add_executable(net_apitest NetUserCommand.c testlist.c)
set_module_type(net_apitest win32cui)
add_importlibs(net_apitest netapi32 advapi32 msvcrt kernel32 ntdll)
add_rostests_file(TARGET net_apitest)
