
add_executable(rdp_apitest RemoteDesktop.c testlist.c)
set_module_type(rdp_apitest win32cui)
add_importlibs(rdp_apitest advapi32 ws2_32 msvcrt kernel32)
add_rostests_file(TARGET rdp_apitest)
