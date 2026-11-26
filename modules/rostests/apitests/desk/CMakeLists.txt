list(APPEND SOURCE
    BackgroundApply.c
    testlist.c)

add_executable(desk_apitest ${SOURCE})
target_link_libraries(desk_apitest wine)
set_module_type(desk_apitest win32cui)
add_importlibs(desk_apitest user32 msvcrt kernel32)
add_rostests_file(TARGET desk_apitest)
