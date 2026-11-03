
list(APPEND SOURCE
    DriverLoad.c
    testlist.c)

add_executable(msacm32_apitest ${SOURCE})
target_link_libraries(msacm32_apitest wine ${PSEH_LIB})
set_module_type(msacm32_apitest win32cui)
add_importlibs(msacm32_apitest msvcrt kernel32)
add_rostests_file(TARGET msacm32_apitest)
