
add_executable(d3d11_apitest hal_render.c testlist.c)
target_link_libraries(d3d11_apitest wine dxguid uuid)
set_module_type(d3d11_apitest win32cui)
add_dependencies(d3d11_apitest d3d_idl_headers)
add_importlibs(d3d11_apitest d3d11 dxgi d3dcompiler_47 user32 msvcrt kernel32)
add_rostests_file(TARGET d3d11_apitest)
