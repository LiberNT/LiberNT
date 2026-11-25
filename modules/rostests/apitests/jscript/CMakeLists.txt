
list(APPEND SOURCE
    NumberConversion.c
    testlist.c)

add_executable(jscript_apitest ${SOURCE})
target_link_libraries(jscript_apitest wine uuid)
set_module_type(jscript_apitest win32cui)
add_importlibs(jscript_apitest ole32 oleaut32 msvcrt kernel32)
add_rostests_file(TARGET jscript_apitest)
