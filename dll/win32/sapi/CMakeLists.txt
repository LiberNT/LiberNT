
spec2def(sapi.dll sapi.spec)

list(APPEND SOURCE
    async.c
    automation.c
    dispatch.c
    main.c
    mmaudio.c
    resource.c
    stream.c
    token.c
    tts.c
    xml.c)

set_source_files_properties(sapi.rc PROPERTIES OBJECT_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/sapi.rgs)

add_library(sapi MODULE
    ${SOURCE}
    sapi.rc
    ${CMAKE_CURRENT_BINARY_DIR}/sapi_stubs.c
    ${CMAKE_CURRENT_BINARY_DIR}/sapi.def)

add_typelib(sapi_typelib.idl)
add_idl_reg_scripts(sapi registry sapi_classes.idl)
add_idl_reg_scripts(sapi regtypelib sapi_typelib.idl)
add_dependencies(sapi psdk stdole2)
set_module_type(sapi win32dll)
target_compile_definitions(sapi PRIVATE __WINESRC__)
target_link_libraries(sapi uuid mfuuid wmcodecdspuuid wine wine_dll_canunload wine_dll_register)
add_importlibs(sapi ole32 oleaut32 user32 advapi32 msvcrt kernel32 ntdll)
add_delay_importlibs(sapi winmm mfplat)
add_cd_file(TARGET sapi DESTINATION reactos/system32/Speech/Common FOR all)
set_wine_module_FIXME(sapi)
