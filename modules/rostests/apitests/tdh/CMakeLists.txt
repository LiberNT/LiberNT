
list(APPEND SOURCE
    TdhProperty.c
    testlist.c)

add_executable(tdh_apitest ${SOURCE})
set_module_type(tdh_apitest win32cui)
add_importlibs(tdh_apitest tdh msvcrt kernel32)
add_rostests_file(TARGET tdh_apitest)
