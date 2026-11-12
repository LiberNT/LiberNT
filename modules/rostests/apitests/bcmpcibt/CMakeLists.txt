
add_executable(bcmpcibt_apitest BcmPciBt.c testlist.c)
set_module_type(bcmpcibt_apitest win32cui)
add_importlibs(bcmpcibt_apitest msvcrt kernel32 ntdll)
add_rostests_file(TARGET bcmpcibt_apitest)
