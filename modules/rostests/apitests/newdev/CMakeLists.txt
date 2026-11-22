add_executable(newdev_apitest
    protocol.c
    UninstallDriver.c
    UpdateDriver.c
    testlist.c)

set_module_type(newdev_apitest win32cui)
add_importlibs(newdev_apitest newdev setupapi user32 msvcrt kernel32)
add_rostests_file(TARGET newdev_apitest)
