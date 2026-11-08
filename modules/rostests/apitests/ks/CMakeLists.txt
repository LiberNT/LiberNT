
add_executable(ks_apitest SizedList.c testlist.c)
target_link_libraries(ks_apitest wine uuid)
set_module_type(ks_apitest win32cui)
add_importlibs(ks_apitest setupapi msvcrt kernel32 ntdll)
add_rostests_file(TARGET ks_apitest)
