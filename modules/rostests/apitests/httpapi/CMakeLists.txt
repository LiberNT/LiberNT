
list(APPEND SOURCE
    httpapi.c)

list(APPEND PCH_SKIP_SOURCE
    testlist.c)

add_executable(httpapi_apitest
    ${SOURCE}
    ${PCH_SKIP_SOURCE})

target_link_libraries(httpapi_apitest wine)
set_module_type(httpapi_apitest win32cui)
add_importlibs(httpapi_apitest httpapi ws2_32 advapi32 msvcrt kernel32 ntdll)
add_rostests_file(TARGET httpapi_apitest)
