
add_executable(monitor_apitest LcdBrightness.c testlist.c)
set_module_type(monitor_apitest win32cui)
add_importlibs(monitor_apitest msvcrt kernel32)
add_rostests_file(TARGET monitor_apitest)
