
list(APPEND SOURCE
    MapFont.c
    testlist.c)

add_executable(mlang_apitest ${SOURCE})
target_link_libraries(mlang_apitest uuid)
set_module_type(mlang_apitest win32cui)
add_importlibs(mlang_apitest ole32 user32 gdi32 msvcrt kernel32)
add_rostests_file(TARGET mlang_apitest)
