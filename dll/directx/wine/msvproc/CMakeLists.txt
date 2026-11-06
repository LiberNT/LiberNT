
spec2def(msvproc.dll msvproc.spec)

include_directories(${REACTOS_SOURCE_DIR}/dll/directx/wine/colorcnv)

add_library(msvproc MODULE
    video_processor.c
    msvproc.c
    ${REACTOS_SOURCE_DIR}/dll/directx/wine/colorcnv/pixels.c
    ${CMAKE_CURRENT_BINARY_DIR}/msvproc_stubs.c
    ${CMAKE_CURRENT_BINARY_DIR}/msvproc.def)

add_idl_reg_scripts(msvproc registry msvproc.idl)
set_module_type(msvproc win32dll)
target_link_libraries(msvproc mfuuid dmoguids dxguid strmiids wmcodecdspuuid uuid wine wine_dll_canunload wine_dll_register oldnames)
add_importlibs(msvproc mfplat ole32 msvcrt kernel32 ntdll)
add_dependencies(msvproc wineheaders dxsdk)
add_cd_file(TARGET msvproc DESTINATION reactos/system32 FOR all)
set_wine_module_FIXME(msvproc) # CORE-5743: No CONST_VTABLE
