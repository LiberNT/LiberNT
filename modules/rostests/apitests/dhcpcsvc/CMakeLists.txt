
list(APPEND SOURCE
    DhcpAcquireParameters.c
    testlist.c)

add_executable(dhcpcsvc_apitest ${SOURCE})
target_link_libraries(dhcpcsvc_apitest wine)
set_module_type(dhcpcsvc_apitest win32cui)
add_importlibs(dhcpcsvc_apitest iphlpapi ws2_32 msvcrt kernel32 ntdll)
add_rostests_file(TARGET dhcpcsvc_apitest)
