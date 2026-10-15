
list(APPEND SOURCE
    WinStationConnectAndLockDesktop.c
    testlist.c)

add_executable(winsta_apitest ${SOURCE})
target_link_libraries(winsta_apitest wine ${PSEH_LIB})
set_module_type(winsta_apitest win32cui)
add_importlibs(winsta_apitest msvcrt kernel32)
add_rostests_file(TARGET winsta_apitest)
