
include_directories(${REACTOS_SOURCE_DIR}/sdk/include/reactos/idl ${CMAKE_CURRENT_BINARY_DIR})
add_rpc_files(client ${REACTOS_SOURCE_DIR}/sdk/include/reactos/idl/scardsvr.idl)
spec2def(winscard.dll winscard.spec ADD_IMPORTLIB)

list(APPEND SOURCE
    winscard.c
    scardsvc.c
    ${CMAKE_CURRENT_BINARY_DIR}/scardsvr_c.c
    ${CMAKE_CURRENT_BINARY_DIR}/winscard_stubs.c)

add_library(winscard MODULE
    ${SOURCE}
    winscard.rc
    ${CMAKE_CURRENT_BINARY_DIR}/winscard.def)

set_module_type(winscard win32dll)
target_link_libraries(winscard wine ${PSEH_LIB})
add_importlibs(winscard rpcrt4 msvcrt kernel32 ntdll)
add_cd_file(TARGET winscard DESTINATION reactos/system32 FOR all)
set_wine_module(winscard)
