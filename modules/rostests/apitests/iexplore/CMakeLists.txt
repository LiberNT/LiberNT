
add_executable(iexplore_apitest Registration.c testlist.c)
set_module_type(iexplore_apitest win32cui)
add_importlibs(iexplore_apitest shlwapi advapi32 msvcrt kernel32)
add_rostests_file(TARGET iexplore_apitest)
