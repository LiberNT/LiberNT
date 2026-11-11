list(APPEND SOURCE
    ustring.c
    testlist.c)

add_executable(icu_apitest ${SOURCE})
set_module_type(icu_apitest win32cui)
add_importlibs(icu_apitest msvcrt kernel32)
add_rostests_file(TARGET icu_apitest)
