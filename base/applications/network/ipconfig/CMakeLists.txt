
include_directories(${REACTOS_SOURCE_DIR}/sdk/include/libernt/idl)
add_rpc_files(client ${REACTOS_SOURCE_DIR}/sdk/include/libernt/idl/dhcpcsvc.idl)

add_executable(ipconfig ipconfig.c ipconfig.rc ${CMAKE_CURRENT_BINARY_DIR}/dhcpcsvc_c.c)
set_module_type(ipconfig win32cui UNICODE)
target_link_libraries(ipconfig conutils)
add_importlibs(ipconfig iphlpapi dnsapi dhcpcsvc rpcrt4 advapi32 msvcrt kernel32 ntdll)
add_cd_file(TARGET ipconfig DESTINATION reactos/system32 FOR all)
