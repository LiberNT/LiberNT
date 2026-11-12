
include_directories(${REACTOS_SOURCE_DIR}/sdk/include/reactos/idl ${CMAKE_CURRENT_BINARY_DIR})
add_rpc_files(server ${REACTOS_SOURCE_DIR}/sdk/include/reactos/idl/scardsvr.idl)

list(APPEND SOURCE
    scardsvr.c
    rpcserver.c
    scardsvr.h
    ${CMAKE_CURRENT_BINARY_DIR}/scardsvr_s.c)

add_executable(scardsvr ${SOURCE} scardsvr.rc)
set_module_type(scardsvr win32cui UNICODE)
add_importlibs(scardsvr advapi32 setupapi user32 rpcrt4 msvcrt kernel32 ntdll)
add_cd_file(TARGET scardsvr DESTINATION reactos/system32 FOR all)
