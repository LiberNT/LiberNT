add_definitions(-D__WINESRC__)
add_definitions(-DWINE_REGISTER_DLL -DPROXY_CLSID=CLSID_PSFactoryBuffer -DENTRY_PREFIX=msdaps_)
spec2def(msdaps.dll msdaps.spec)
include_directories(${CMAKE_CURRENT_BINARY_DIR})
add_idl_headers(msdaps_idlheader row_server.idl)

add_rpcproxy_files(
    msdaps.idl
    row_server.idl)

list(APPEND SOURCE
    main.c
    row_server.c
    usrmarshal.c
    ${CMAKE_CURRENT_BINARY_DIR}/msdaps_p.c
    ${CMAKE_CURRENT_BINARY_DIR}/row_server_p.c
    ${CMAKE_CURRENT_BINARY_DIR}/proxy.dlldata.c)

add_library(msdaps MODULE
    ${SOURCE}
    ${CMAKE_CURRENT_BINARY_DIR}/msdaps.def)

set_module_type(msdaps win32dll)
add_dependencies(msdaps msdaps_idlheader)
add_idl_reg_scripts(msdaps registry
    msdaps.idl
    row_server.idl)
target_link_libraries(msdaps uuid wine ${PSEH_LIB})
add_importlibs(msdaps rpcrt4 ole32 oleaut32 advapi32 msvcrt kernel32 ntdll)
add_cd_file(TARGET msdaps DESTINATION reactos/system32 FOR all)
