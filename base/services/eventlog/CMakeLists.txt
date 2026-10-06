
include_directories(
    ${REACTOS_SOURCE_DIR}/sdk/lib/evtlib
    ${REACTOS_SOURCE_DIR}/sdk/include/reactos/subsys
    ${REACTOS_SOURCE_DIR}/sdk/include/reactos/idl
    ${CMAKE_CURRENT_BINARY_DIR})

add_rpc_files(server ${REACTOS_SOURCE_DIR}/sdk/include/reactos/idl/eventlogrpc.idl)
spec2def(wevtsvc.dll wevtsvc.spec)

list(APPEND SOURCE
    eventlog.c
    eventsource.c
    logport.c
    rpc.c
    file.c
    eventlog.h
    ${CMAKE_CURRENT_BINARY_DIR}/eventlogrpc_s.c)

add_library(wevtsvc MODULE ${SOURCE} eventlog.rc ${CMAKE_CURRENT_BINARY_DIR}/wevtsvc.def)
add_pch(wevtsvc eventlog.h SOURCE)

if(NOT MSVC)
    target_link_libraries(wevtsvc ${PSEH_LIB})
endif()

set_module_type(wevtsvc win32dll UNICODE)
target_link_libraries(wevtsvc evtlib)
add_importlibs(wevtsvc advapi32 rpcrt4 msvcrt kernel32 ntdll)
add_cd_file(TARGET wevtsvc DESTINATION reactos/system32 FOR all)
