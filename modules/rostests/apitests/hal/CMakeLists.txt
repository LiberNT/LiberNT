
add_executable(hal_apitest FdtPnp.c testlist.c)
set_module_type(hal_apitest win32cui)
add_importlibs(hal_apitest msvcrt kernel32 ntdll)
add_rostests_file(TARGET hal_apitest)
