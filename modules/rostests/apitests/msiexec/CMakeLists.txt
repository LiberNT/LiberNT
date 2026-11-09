
add_executable(msiexec_apitest restart.c testlist.c)
set_module_type(msiexec_apitest win32cui)
add_importlibs(msiexec_apitest msi ole32 msvcrt kernel32)
add_rostests_file(TARGET msiexec_apitest)
