
list(APPEND SOURCE
    gui.c
    lifecycle.c
    testlist.c)

add_executable(lntget_apitest ${SOURCE})
set_module_type(lntget_apitest win32cui)
add_importlibs(lntget_apitest user32 advapi32 msvcrt kernel32)
add_rostests_file(TARGET lntget_apitest)
