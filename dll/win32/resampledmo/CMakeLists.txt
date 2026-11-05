
spec2def(resampledmo.dll resampledmo.spec)

list(APPEND SOURCE
    dsp.c
    main.c
    resampler.c)

add_library(resampledmo MODULE
    ${SOURCE}
    ${CMAKE_CURRENT_BINARY_DIR}/resampledmo_stubs.c
    ${CMAKE_CURRENT_BINARY_DIR}/resampledmo.def)

add_idl_reg_scripts(resampledmo registry resampledmo_classes.idl)
target_compile_definitions(resampledmo PRIVATE __WINESRC__)
set_module_type(resampledmo win32dll)
target_link_libraries(resampledmo mfuuid dmoguids strmiids wmcodecdspuuid uuid wine wine_dll_canunload wine_dll_register)
add_importlibs(resampledmo mfplat msdmo ole32 oleaut32 msvcrt kernel32 ntdll)
add_dependencies(resampledmo wineheaders dxsdk)
add_cd_file(TARGET resampledmo DESTINATION reactos/system32 FOR all)
set_wine_module(resampledmo)
