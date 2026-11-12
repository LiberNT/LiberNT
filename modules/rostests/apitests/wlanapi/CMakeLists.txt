
add_executable(wlanapi_apitest wlanapi.c adapter.c recovery.c vwifi.c testlist.c)
set_module_type(wlanapi_apitest win32cui)
add_importlibs(wlanapi_apitest wlanapi iphlpapi setupapi newdev advapi32 msvcrt kernel32)
add_rostests_file(TARGET wlanapi_apitest)
