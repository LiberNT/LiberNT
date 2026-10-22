
add_executable(ncrypt_apitest KeyStorage.c testlist.c)
set_module_type(ncrypt_apitest win32cui)
add_importlibs(ncrypt_apitest ncrypt bcrypt msvcrt kernel32)
add_rostests_file(TARGET ncrypt_apitest)
