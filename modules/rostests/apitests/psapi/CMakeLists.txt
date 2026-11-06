
add_executable(psapi_apitest EnumDeviceDrivers.c psapi.c QueryWorkingSetEx.c testlist.c)
set_module_type(psapi_apitest win32cui)
add_importlibs(psapi_apitest psapi msvcrt kernel32 ntdll)
add_rostests_file(TARGET psapi_apitest)
