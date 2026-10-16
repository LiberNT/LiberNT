
spec2def(photometadatahandler.dll photometadatahandler.spec)

list(APPEND SOURCE
    imagepropstore.c
    main.c
    ${CMAKE_CURRENT_BINARY_DIR}/photometadatahandler.def)

add_library(photometadatahandler MODULE ${SOURCE})
add_idl_reg_scripts(photometadatahandler registry classes.idl)
set_module_type(photometadatahandler win32dll)
target_link_libraries(photometadatahandler uuid wine wine_dll_register)
add_importlibs(photometadatahandler propsys windowscodecs ole32 oleaut32 msvcrt kernel32 ntdll)
add_cd_file(TARGET photometadatahandler DESTINATION reactos/system32 FOR all)
