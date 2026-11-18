
list(APPEND SOURCE
    common.c
    decoupled.c
    repository.c
    testlist.c)

add_executable(wbemprox_apitest ${SOURCE})
set_module_type(wbemprox_apitest win32cui)
target_link_libraries(wbemprox_apitest uuid)
add_importlibs(wbemprox_apitest ole32 oleaut32 msvcrt kernel32 ntdll)
add_rostests_file(TARGET wbemprox_apitest)
