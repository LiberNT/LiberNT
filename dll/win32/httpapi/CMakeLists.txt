
spec2def(httpapi.dll httpapi.spec ADD_IMPORTLIB)

list(APPEND SOURCE
    httpapi_main.c
    ${CMAKE_CURRENT_BINARY_DIR}/httpapi_stubs.c
    ${CMAKE_CURRENT_BINARY_DIR}/httpapi.def)

add_library(httpapi MODULE ${SOURCE})
set_module_type(httpapi win32dll)
target_link_libraries(httpapi wine)
add_importlibs(httpapi advapi32 msvcrt kernel32 ntdll)
add_cd_file(TARGET httpapi DESTINATION reactos/system32 FOR all)
