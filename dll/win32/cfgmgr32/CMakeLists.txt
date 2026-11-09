
include_directories(
    ${REACTOS_SOURCE_DIR}/sdk/include/reactos/idl
    ${CMAKE_CURRENT_BINARY_DIR})

add_rpc_files(client ${REACTOS_SOURCE_DIR}/sdk/include/reactos/idl/pnp.idl)
spec2def(cfgmgr32.dll cfgmgr32.spec ADD_IMPORTLIB)

list(APPEND SOURCE
    cfgmgr.c
    cfgmgr32.c
    devobject.c
    notification.c
    rpc.c
    swdevice.c
    ${CMAKE_CURRENT_BINARY_DIR}/pnp_c.c
    cfgmgr32.rc
    ${CMAKE_CURRENT_BINARY_DIR}/cfgmgr32_stubs.c
    ${CMAKE_CURRENT_BINARY_DIR}/cfgmgr32.def)

add_library(cfgmgr32 MODULE ${SOURCE})

set_module_type(cfgmgr32 win32dll)
target_compile_definitions(cfgmgr32 PRIVATE __WINESRC__ _CFGMGR32_)
target_link_libraries(cfgmgr32 wine ${PSEH_LIB})
add_delay_importlibs(cfgmgr32 user32)
add_importlibs(cfgmgr32 advapi32 rpcrt4 sechost msvcrt kernel32 ntdll)
add_dependencies(cfgmgr32 wineheaders)
add_cd_file(TARGET cfgmgr32 DESTINATION reactos/system32 FOR all)
set_wine_module(cfgmgr32)
