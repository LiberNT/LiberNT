add_executable(screencap screencap.c)
set_module_type(screencap win32cui UNICODE)
add_importlibs(screencap winhttp user32 gdi32 msvcrt kernel32)
add_cd_file(TARGET screencap DESTINATION reactos/system32 FOR all)
