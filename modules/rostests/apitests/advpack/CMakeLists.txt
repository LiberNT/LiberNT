
list(APPEND SOURCE
    DelNode.c
    RegInstall.c
    testlist.c)

add_executable(advpack_apitest ${SOURCE} advpack_apitest.rc)
target_link_libraries(advpack_apitest wine ${PSEH_LIB})
set_module_type(advpack_apitest win32cui)
add_importlibs(advpack_apitest advpack advapi32 msvcrt kernel32 ntdll)
add_rostests_file(TARGET advpack_apitest)
