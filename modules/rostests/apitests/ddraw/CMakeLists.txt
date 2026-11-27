
add_executable(ddraw_apitest DirectDrawCreate.c PrimaryBltLatency.c QueryInterface.c testlist.c)
target_link_libraries(ddraw_apitest wine dxguid)
set_module_type(ddraw_apitest win32cui)
add_importlibs(ddraw_apitest ddraw gdi32 user32 msvcrt kernel32)
add_rostests_file(TARGET ddraw_apitest)
