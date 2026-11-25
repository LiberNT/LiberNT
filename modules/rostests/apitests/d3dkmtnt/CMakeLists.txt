
remove_definitions(-DDXGKDDI_INTERFACE_VERSION=0x1052)
add_definitions(-DDXGKDDI_INTERFACE_VERSION=0x11007 -D_DLL -D__USE_CRTIMP)
list(APPEND SOURCE
    sharedaccess.c
    testlist.c)
add_executable(d3dkmtnt_apitest ${SOURCE})
target_link_libraries(d3dkmtnt_apitest wine)
set_module_type(d3dkmtnt_apitest win32cui)
add_importlibs(d3dkmtnt_apitest msvcrt kernel32 ntdll)
add_rostests_file(TARGET d3dkmtnt_apitest)
