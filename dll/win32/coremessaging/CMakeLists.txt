spec2def(coremessaging.dll coremessaging.spec)

list(APPEND SOURCE
    main.c
    queue.c
    weakref.c
    private.h
    weakref.h)

add_library(coremessaging MODULE ${SOURCE} ${CMAKE_CURRENT_BINARY_DIR}/coremessaging.def ${CMAKE_CURRENT_BINARY_DIR}/coremessaging_stubs.c)
add_idl_reg_scripts(coremessaging winrt classes.idl)
set_module_type(coremessaging win32dll)
target_compile_definitions(coremessaging PRIVATE __WINESRC__)
target_link_libraries(coremessaging uuid wine wine_dll_canunload wine_dll_register)
add_importlibs(coremessaging combase user32 msvcrt kernel32 ntdll)
add_cd_file(TARGET coremessaging DESTINATION reactos/system32 FOR all)
set_wine_module_FIXME(coremessaging)
