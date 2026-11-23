
add_executable(regsvr32_apitest wow64.c testlist.c)
set_module_type(regsvr32_apitest win32cui)
add_importlibs(regsvr32_apitest msvcrt kernel32)
add_rostests_file(TARGET regsvr32_apitest)
