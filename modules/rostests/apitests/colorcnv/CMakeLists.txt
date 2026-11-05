
add_executable(colorcnv_apitest convert.c testlist.c)
set_module_type(colorcnv_apitest win32cui)
target_link_libraries(colorcnv_apitest mfuuid uuid)
add_importlibs(colorcnv_apitest mfplat ole32 msvcrt kernel32 ntdll)
add_rostests_file(TARGET colorcnv_apitest)
