
add_executable(mssign32_apitest SignerSign.c testlist.c)
set_module_type(mssign32_apitest win32cui)
add_importlibs(mssign32_apitest crypt32 advapi32 msvcrt kernel32)
add_rostests_file(TARGET mssign32_apitest)
