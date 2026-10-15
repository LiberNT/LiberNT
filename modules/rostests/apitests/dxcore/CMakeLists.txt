
list(APPEND SOURCE
    DXCoreCreateAdapterFactory.cpp
    testlist.c)

add_executable(dxcore_apitest ${SOURCE})
target_link_libraries(dxcore_apitest cpprt uuid)
set_module_type(dxcore_apitest win32cui)
add_importlibs(dxcore_apitest dxcore msvcrt kernel32 ntdll)
add_rostests_file(TARGET dxcore_apitest)
