
add_executable(dwrite_apitest HasVariations.c testlist.c)
set_module_type(dwrite_apitest win32cui)
add_importlibs(dwrite_apitest dwrite msvcrt kernel32)
add_rostests_file(TARGET dwrite_apitest)
