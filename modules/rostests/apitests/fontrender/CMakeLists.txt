add_executable(fontrender_apitest common.c DWriteText.c GdiText.c testlist.c fontrender.rc)
set_module_type(fontrender_apitest win32cui)
add_importlibs(fontrender_apitest dwrite gdi32 user32 msvcrt kernel32)
add_rostests_file(TARGET fontrender_apitest)
