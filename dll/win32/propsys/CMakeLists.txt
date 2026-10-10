
add_definitions(-D_PROPSYS_)
spec2def(propsys.dll propsys.spec ADD_IMPORTLIB)
add_rpcproxy_files(propsys_propsys.idl)
generate_idl_iids(propsys_propsys.idl)

list(APPEND SOURCE
    propstore.c
    propsys_main.c
    propvar.c
    precomp.h
    ${CMAKE_CURRENT_BINARY_DIR}/propsys_stubs.c)

list(APPEND PROXY_SOURCE
    propsys_proxy.c
    ${CMAKE_CURRENT_BINARY_DIR}/propsys_propsys_i.c
    ${CMAKE_CURRENT_BINARY_DIR}/propsys_propsys_p.c
    ${CMAKE_CURRENT_BINARY_DIR}/proxy.dlldata.c)

set_source_files_properties(${PROXY_SOURCE} PROPERTIES
    COMPILE_DEFINITIONS "PROXY_DELEGATION;PROXY_CLSID=CLSID_PSFactoryBuffer;ENTRY_PREFIX=propsys_ps_"
    SKIP_PRECOMPILE_HEADERS ON)

add_library(propsys MODULE
    ${SOURCE}
    ${PROXY_SOURCE}
    ${CMAKE_CURRENT_BINARY_DIR}/propsys.def)

add_idl_reg_scripts(propsys registry propsys_classes.idl propsys_propsys.idl)
target_compile_definitions(propsys PRIVATE _FORCENAMELESSUNION)
set_module_type(propsys win32dll)
target_link_libraries(propsys uuid wine wine_dll_register wine_dll_canunload)
add_importlibs(propsys rpcrt4 user32 ole32 oleaut32 msvcrt kernel32 ntdll)
add_pch(propsys precomp.h SOURCE)
add_cd_file(TARGET propsys DESTINATION reactos/system32 FOR all)
set_wine_module_FIXME(propsys) # CORE-5743: No ARRAY_SIZE and CONST_VTABLE
