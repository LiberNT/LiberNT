
spec2def(appxdeploymentclient.dll appxdeploymentclient.spec)

list(APPEND SOURCE
    main.c
    package.c
    private.h)

add_library(appxdeploymentclient MODULE ${SOURCE} ${CMAKE_CURRENT_BINARY_DIR}/appxdeploymentclient.def ${CMAKE_CURRENT_BINARY_DIR}/appxdeploymentclient_stubs.c)
add_idl_reg_scripts(appxdeploymentclient winrt classes.idl)
set_module_type(appxdeploymentclient win32dll)
target_compile_definitions(appxdeploymentclient PRIVATE __WINESRC__)
target_link_libraries(appxdeploymentclient uuid wine wine_dll_canunload wine_dll_register)
add_importlibs(appxdeploymentclient combase msvcrt kernel32 ntdll)
add_dependencies(appxdeploymentclient psdk)
add_cd_file(TARGET appxdeploymentclient DESTINATION reactos/system32 FOR all)
set_wine_module_FIXME(appxdeploymentclient)
