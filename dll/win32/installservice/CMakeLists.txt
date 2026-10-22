
spec2def(installservice.dll installservice.spec)

list(APPEND SOURCE
    async.c
    main.c
    manager.c
    options.c
    private.h)

add_library(installservice MODULE ${SOURCE} installservice.rc ${CMAKE_CURRENT_BINARY_DIR}/installservice.def)
add_idl_reg_scripts(installservice winrt classes.idl)
set_module_type(installservice win32dll)
target_link_libraries(installservice uuid wine wine_dll_canunload wine_dll_register)
add_importlibs(installservice combase ole32 advapi32 msvcrt kernel32 ntdll)
add_dependencies(installservice psdk)
add_cd_file(TARGET installservice DESTINATION reactos/system32 FOR all)
