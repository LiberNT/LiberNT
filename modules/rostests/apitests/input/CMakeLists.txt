
list(APPEND SOURCE
    layout_or_tip.c
    testlist.c)

add_executable(input_apitest ${SOURCE})
target_link_libraries(input_apitest wine)
set_module_type(input_apitest win32cui)
add_importlibs(input_apitest advapi32 user32 msvcrt kernel32)
add_rostests_file(TARGET input_apitest)
