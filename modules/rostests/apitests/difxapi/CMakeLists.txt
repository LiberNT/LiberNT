
list(APPEND SOURCE
    DriverPackage.c
    testlist.c)

add_executable(difxapi_apitest ${SOURCE})
target_link_libraries(difxapi_apitest wine)
set_module_type(difxapi_apitest win32cui)
add_importlibs(difxapi_apitest msvcrt kernel32 ntdll)
add_rostests_file(TARGET difxapi_apitest)
