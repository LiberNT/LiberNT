add_executable(d2d1_apitest BitmapSourceEffect.c FillMesh.c GeometryOps.c Layer.c Path.c PrimitiveBlend.c testlist.c)
set_module_type(d2d1_apitest win32cui)
add_importlibs(d2d1_apitest d2d1 d3d11 ole32 msvcrt kernel32)
add_rostests_file(TARGET d2d1_apitest)
