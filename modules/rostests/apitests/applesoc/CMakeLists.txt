
add_executable(applesoc_apitest AppleSoc.c testlist.c)
set_module_type(applesoc_apitest win32cui)
add_importlibs(applesoc_apitest msvcrt kernel32 ntdll)
add_rostests_file(TARGET applesoc_apitest)
