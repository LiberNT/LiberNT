
include_directories(${CMAKE_CURRENT_BINARY_DIR})
set(OLD_IDL_FLAGS ${IDL_FLAGS})
set(IDL_FLAGS ${IDL_FLAGS} --prefix-server=s_ -Oicf)
add_rpc_files(client embedded.idl)
add_rpc_files(server embedded.idl)
set(IDL_FLAGS ${OLD_IDL_FLAGS})

list(APPEND SOURCE
    EmbeddedPointers.c
    testlist.c
    ${CMAKE_CURRENT_BINARY_DIR}/embedded_c.c
    ${CMAKE_CURRENT_BINARY_DIR}/embedded_s.c)

add_executable(rpcrt4_apitest ${SOURCE})
target_link_libraries(rpcrt4_apitest wine ${PSEH_LIB})
set_module_type(rpcrt4_apitest win32cui)
add_importlibs(rpcrt4_apitest rpcrt4 msvcrt kernel32 ntdll)
add_rostests_file(TARGET rpcrt4_apitest)
