
add_executable(d3d9_apitest hal_render.c testlist.c)
target_link_libraries(d3d9_apitest wine)
set_module_type(d3d9_apitest win32cui)
add_importlibs(d3d9_apitest d3d9 gdi32 user32 msvcrt kernel32)
add_rostests_file(TARGET d3d9_apitest)
