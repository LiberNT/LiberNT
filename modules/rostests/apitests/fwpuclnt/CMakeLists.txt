
list(APPEND SOURCE
    ale.c
    mpssvc.c)

list(APPEND PCH_SKIP_SOURCE
    testlist.c)

add_executable(fwpuclnt_apitest
    ${SOURCE}
    ${PCH_SKIP_SOURCE}
    $<TARGET_OBJECTS:mpssvc_policy>)

target_include_directories(fwpuclnt_apitest PRIVATE ${REACTOS_SOURCE_DIR}/base/services/mpssvc)
target_link_libraries(fwpuclnt_apitest wine)
set_module_type(fwpuclnt_apitest win32cui)
add_importlibs(fwpuclnt_apitest fwpuclnt ws2_32 advapi32 msvcrt kernel32 ntdll)
add_rostests_file(TARGET fwpuclnt_apitest)
