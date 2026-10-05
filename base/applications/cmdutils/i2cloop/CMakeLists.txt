add_executable(i2cloop i2cloop.c i2cloop.rc)
set_module_type(i2cloop win32cui)
add_importlibs(i2cloop setupapi msvcrt kernel32)
add_cd_file(TARGET i2cloop DESTINATION reactos/system32 FOR all)
