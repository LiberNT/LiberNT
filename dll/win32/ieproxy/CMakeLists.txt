add_definitions(-D__WINESRC__)
add_definitions(-DWINE_REGISTER_DLL -DPROXY_DELEGATION -DPROXY_CLSID=CLSID_PSFactoryBuffer)
spec2def(ieproxy.dll ieproxy.spec)
generate_idl_iids(ieproxy_ieautomation.idl)

add_rpcproxy_files(
    ieproxy_ieautomation.idl
    ieproxy_perhist.idl
    ieproxy_priv.idl)

list(APPEND SOURCE
    ${CMAKE_CURRENT_BINARY_DIR}/ieproxy_ieautomation_i.c
    ${CMAKE_CURRENT_BINARY_DIR}/ieproxy_ieautomation_p.c
    ${CMAKE_CURRENT_BINARY_DIR}/ieproxy_perhist_p.c
    ${CMAKE_CURRENT_BINARY_DIR}/ieproxy_priv_p.c
    ${CMAKE_CURRENT_BINARY_DIR}/proxy.dlldata.c)

add_library(ieproxy MODULE
    ${SOURCE}
    ${CMAKE_CURRENT_BINARY_DIR}/ieproxy.def)

set_module_type(ieproxy win32dll)
add_idl_reg_scripts(ieproxy registry
    ieproxy_ieautomation.idl
    ieproxy_perhist.idl
    ieproxy_priv.idl)
target_link_libraries(ieproxy uuid wine ${PSEH_LIB})
add_importlibs(ieproxy rpcrt4 ole32 oleaut32 msvcrt kernel32 ntdll)
add_cd_file(TARGET ieproxy DESTINATION reactos/system32 FOR all)
