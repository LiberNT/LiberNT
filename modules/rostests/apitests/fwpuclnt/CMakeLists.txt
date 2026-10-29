
list(APPEND SOURCE
    ale.c)

list(APPEND PCH_SKIP_SOURCE
    testlist.c)

add_executable(fwpuclnt_apitest
    ${SOURCE}
    ${PCH_SKIP_SOURCE})

target_link_libraries(fwpuclnt_apitest wine)
set_module_type(fwpuclnt_apitest win32cui)
add_importlibs(fwpuclnt_apitest fwpuclnt ws2_32 msvcrt kernel32 ntdll)
add_rostests_file(TARGET fwpuclnt_apitest)
