
list(APPEND SOURCE
    cmd.c
    confirm.c
    dir.c
    fc.c
    script.c
    tree.c
    where.c
    testlist.c)

add_executable(cmd_apitest ${SOURCE})
target_link_libraries(cmd_apitest wine ${PSEH_LIB})
set_module_type(cmd_apitest win32cui)
add_importlibs(cmd_apitest shlwapi user32 msvcrt kernel32)
add_rostests_file(TARGET cmd_apitest)
