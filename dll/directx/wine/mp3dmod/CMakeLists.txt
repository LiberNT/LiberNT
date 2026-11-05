
include_directories(${REACTOS_SOURCE_DIR}/sdk/include/reactos/libs/libmpg123)
spec2def(mp3dmod.dll mp3dmod.spec)

add_library(mp3dmod MODULE
    mp3dmod.c
    ${CMAKE_CURRENT_BINARY_DIR}/mp3dmod_stubs.c
    ${CMAKE_CURRENT_BINARY_DIR}/mp3dmod.def)

add_idl_reg_scripts(mp3dmod registry mp3dmod.idl)
set_module_type(mp3dmod win32dll)
target_compile_definitions(mp3dmod PRIVATE MPG123_NO_LARGENAME)
target_link_libraries(mp3dmod libmpg123 mfuuid dmoguids wmcodecdspuuid uuid wine wine_dll_canunload wine_dll_register oldnames)
add_importlibs(mp3dmod mfplat msdmo ole32 kernelbase msvcrt kernel32 ntdll)
add_dependencies(mp3dmod wineheaders dxsdk)
add_cd_file(TARGET mp3dmod DESTINATION reactos/system32 FOR all)
set_wine_module_FIXME(mp3dmod) # CORE-5743: No CONST_VTABLE
