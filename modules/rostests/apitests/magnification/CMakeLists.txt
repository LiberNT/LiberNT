
list(APPEND SOURCE
    MagnifierWindow.c
    testlist.c)

add_executable(magnification_apitest ${SOURCE})
set_module_type(magnification_apitest win32cui)
add_importlibs(magnification_apitest user32 msvcrt kernel32)
add_rostests_file(TARGET magnification_apitest)
