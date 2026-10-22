
add_executable(netprofm_apitest GetCost.c testlist.c)
target_link_libraries(netprofm_apitest uuid)
set_module_type(netprofm_apitest win32cui)
add_importlibs(netprofm_apitest ole32 iphlpapi ws2_32 advapi32 msvcrt kernel32)
add_rostests_file(TARGET netprofm_apitest)
