
list(APPEND SOURCE
    InternetShortcutSave.c
    testlist.c)

add_executable(ieframe_apitest ${SOURCE})
set_module_type(ieframe_apitest win32cui)
target_link_libraries(ieframe_apitest uuid)
add_importlibs(ieframe_apitest shlwapi ole32 msvcrt kernel32)
add_rostests_file(TARGET ieframe_apitest)
