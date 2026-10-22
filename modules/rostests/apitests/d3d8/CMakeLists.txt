add_executable(d3d8_apitest LinePattern.c testlist.c)
set_module_type(d3d8_apitest win32cui)
add_importlibs(d3d8_apitest user32 msvcrt kernel32)
add_rostests_file(TARGET d3d8_apitest)
