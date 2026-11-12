list(APPEND SOURCE
    Cancel.c
    Device.c
    helpers.c
    Transfer.c
    testlist.c)

add_executable(winusb_apitest ${SOURCE})
target_link_libraries(winusb_apitest wine uuid)
set_module_type(winusb_apitest win32cui)
add_importlibs(winusb_apitest winusb setupapi cfgmgr32 msvcrt kernel32 ntdll)
add_rostests_file(TARGET winusb_apitest)
