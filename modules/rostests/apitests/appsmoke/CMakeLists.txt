
remove_definitions(-DDXGKDDI_INTERFACE_VERSION=0x1052)
add_definitions(-DDXGKDDI_INTERFACE_VERSION=0x11007)

add_executable(appsmoke_apitest
    d3dkmt.c
    display.c
    graphics.cpp
    opencl.c
    process.c
    sockets.c
    suite.c
    testlist.c)

target_include_directories(appsmoke_apitest PRIVATE ${REACTOS_SOURCE_DIR}/modules/rostests/apitests/include)
set_module_type(appsmoke_apitest win32cui)
add_importlibs(appsmoke_apitest user32 gdi32 ws2_32 msvcrt kernel32 ntdll)
add_rostests_file(TARGET appsmoke_apitest)
