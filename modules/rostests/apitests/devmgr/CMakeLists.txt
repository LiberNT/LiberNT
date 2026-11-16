
list(APPEND SOURCE
    DeviceStatus.c
    testlist.c)

add_executable(devmgr_apitest ${SOURCE})
set_module_type(devmgr_apitest win32cui)
add_importlibs(devmgr_apitest setupapi cfgmgr32 user32 msvcrt kernel32)
add_rostests_file(TARGET devmgr_apitest)
