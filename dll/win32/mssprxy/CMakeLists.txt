
add_definitions(-D__WINESRC__)
add_definitions(-DWINE_REGISTER_DLL -DPROXY_DELEGATION -DPROXY_CLSID=CLSID_PSFactoryBuffer)
spec2def(mssprxy.dll mssprxy.spec)
generate_idl_iids(mssprxy_filter.idl)

add_rpcproxy_files(mssprxy_filter.idl)

list(APPEND SOURCE
    ${CMAKE_CURRENT_BINARY_DIR}/mssprxy_filter_i.c
    ${CMAKE_CURRENT_BINARY_DIR}/mssprxy_filter_p.c
    ${CMAKE_CURRENT_BINARY_DIR}/proxy.dlldata.c)

add_library(mssprxy MODULE
    ${SOURCE}
    ${CMAKE_CURRENT_BINARY_DIR}/mssprxy.def)

set_module_type(mssprxy win32dll)
add_idl_reg_scripts(mssprxy registry mssprxy_filter.idl)
target_link_libraries(mssprxy uuid wine ${PSEH_LIB})
add_importlibs(mssprxy rpcrt4 ole32 oleaut32 msvcrt kernel32 ntdll)
add_cd_file(TARGET mssprxy DESTINATION reactos/system32 FOR all)
