
list(APPEND SOURCE
    NodeConnectionInfo.c
    testlist.c)

add_executable(usbhub_apitest ${SOURCE})
target_link_libraries(usbhub_apitest wine ${PSEH_LIB})
set_module_type(usbhub_apitest win32cui)
add_importlibs(usbhub_apitest setupapi msvcrt kernel32 ntdll)
add_rostests_file(TARGET usbhub_apitest)
