
spec2def(dsdmo.dll dsdmo.spec)

add_library(dsdmo MODULE
    main.c
    dsdmo.rc
    ${CMAKE_CURRENT_BINARY_DIR}/dsdmo.def)

add_idl_reg_scripts(dsdmo registry dsdmo.idl)
set_module_type(dsdmo win32dll)
target_link_libraries(dsdmo dmoguids strmiids uuid wine wine_dll_canunload wine_dll_register)
add_importlibs(dsdmo ole32 oleaut32 msvcrt kernel32 ntdll)
add_dependencies(dsdmo wineheaders)
add_cd_file(TARGET dsdmo DESTINATION reactos/system32 FOR all)
set_wine_module_FIXME(dsdmo) # CORE-5743: No ARRAY_SIZE and CONST_VTABLE
