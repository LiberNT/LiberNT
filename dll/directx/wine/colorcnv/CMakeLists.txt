
spec2def(colorcnv.dll colorcnv.spec)

add_library(colorcnv MODULE
    color_converter.c
    pixels.c
    colorcnv.c
    ${CMAKE_CURRENT_BINARY_DIR}/colorcnv_stubs.c
    ${CMAKE_CURRENT_BINARY_DIR}/colorcnv.def)

add_idl_reg_scripts(colorcnv registry colorcnv.idl)
set_module_type(colorcnv win32dll)
target_link_libraries(colorcnv mfuuid dmoguids strmiids wmcodecdspuuid uuid wine wine_dll_canunload wine_dll_register oldnames)
add_importlibs(colorcnv mfplat msdmo ole32 msvcrt kernel32 ntdll)
add_dependencies(colorcnv wineheaders dxsdk)
add_cd_file(TARGET colorcnv DESTINATION reactos/system32 FOR all)
set_wine_module_FIXME(colorcnv) # CORE-5743: No CONST_VTABLE
