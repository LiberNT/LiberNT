
add_definitions(-D__WINESRC__)
add_definitions(-DWINE_REGISTER_DLL -DPROXY_CLSID=CLSID_PSFactoryBuffer)
spec2def(msctfp.dll msctfp.spec)
generate_idl_iids(msctfp_msctf.idl)

# TODO: Proxies of the ctfutb.idl interfaces (it and msctf.idl import each other)
add_rpcproxy_files(
    msctfp_msctf.idl
    msctfp_textstor.idl
    msctfp_inputscope.idl)

list(APPEND SOURCE
    guid.c
    ${CMAKE_CURRENT_BINARY_DIR}/msctfp_msctf_i.c
    ${CMAKE_CURRENT_BINARY_DIR}/msctfp_msctf_p.c
    ${CMAKE_CURRENT_BINARY_DIR}/msctfp_textstor_p.c
    ${CMAKE_CURRENT_BINARY_DIR}/msctfp_inputscope_p.c
    ${CMAKE_CURRENT_BINARY_DIR}/proxy.dlldata.c)

add_library(msctfp MODULE
    ${SOURCE}
    ${CMAKE_CURRENT_BINARY_DIR}/msctfp.def)

set_module_type(msctfp win32dll)
add_idl_reg_scripts(msctfp registry
    msctfp_msctf.idl
    msctfp_textstor.idl
    msctfp_inputscope.idl)
target_link_libraries(msctfp uuid wine ${PSEH_LIB})
add_importlibs(msctfp rpcrt4 ole32 oleaut32 msvcrt kernel32 ntdll)
add_cd_file(TARGET msctfp DESTINATION reactos/system32 FOR all)
