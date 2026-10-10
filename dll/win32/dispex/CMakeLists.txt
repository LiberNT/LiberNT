add_definitions(-D__WINESRC__)
add_definitions(-DWINE_REGISTER_DLL -DPROXY_DELEGATION)
spec2def(dispex.dll dispex.spec)

add_rpcproxy_files(disp_ex.idl)

list(APPEND SOURCE
    usrmarshal.c
    ${CMAKE_CURRENT_BINARY_DIR}/disp_ex_p.c
    ${CMAKE_CURRENT_BINARY_DIR}/proxy.dlldata.c)

add_library(dispex MODULE
    ${SOURCE}
    ${CMAKE_CURRENT_BINARY_DIR}/dispex.def)

set_module_type(dispex win32dll)
add_idl_reg_scripts(dispex registry disp_ex.idl)
target_link_libraries(dispex uuid wine ${PSEH_LIB})
add_importlibs(dispex rpcrt4 ole32 oleaut32 advapi32 msvcrt kernel32 ntdll)
add_cd_file(TARGET dispex DESTINATION reactos/system32 FOR all)
