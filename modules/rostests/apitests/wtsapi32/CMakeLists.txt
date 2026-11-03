
list(APPEND SOURCE
    SessionInformation.c
    testlist.c)

add_executable(wtsapi32_apitest ${SOURCE})
set_module_type(wtsapi32_apitest win32cui)
add_importlibs(wtsapi32_apitest wtsapi32 advapi32 msvcrt kernel32)
add_rostests_file(TARGET wtsapi32_apitest)
