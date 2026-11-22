
list(APPEND SOURCE
    ExecXmlEmpty.c
    RunTask.c
    testlist.c)

add_executable(taskschd_apitest ${SOURCE})
target_link_libraries(taskschd_apitest uuid wine)
set_module_type(taskschd_apitest win32cui)
add_importlibs(taskschd_apitest oleaut32 ole32 msvcrt kernel32 ntdll)
add_rostests_file(TARGET taskschd_apitest)
