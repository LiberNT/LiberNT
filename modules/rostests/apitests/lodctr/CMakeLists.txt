
list(APPEND SOURCE
    manifest.c
    testlist.c)

add_executable(lodctr_apitest ${SOURCE})
set_module_type(lodctr_apitest win32cui)
add_importlibs(lodctr_apitest advapi32 msvcrt kernel32)
add_rostests_file(TARGET lodctr_apitest)
