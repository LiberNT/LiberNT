
project(SHELL)

add_definitions(-D_NETSHELL_)
spec2def(netshell.dll netshell.spec)
add_rpcproxy_files(netshell_netcon.idl)
generate_idl_iids(netshell_netcon.idl)

list(APPEND SOURCE
    netshell.cpp
    shfldr_netconnect.cpp
    enumlist.cpp
    connectmanager.cpp
    lanconnectui.cpp
    lanstatusui.cpp
    setup.cpp)

list(APPEND PCH_SKIP_SOURCE
    ${CMAKE_CURRENT_BINARY_DIR}/netshell_netcon_i.c
    ${CMAKE_CURRENT_BINARY_DIR}/netshell_netcon_p.c
    ${CMAKE_CURRENT_BINARY_DIR}/proxy.dlldata.c
    ${CMAKE_CURRENT_BINARY_DIR}/netshell_stubs.c)

set_source_files_properties(
    ${CMAKE_CURRENT_BINARY_DIR}/netshell_netcon_p.c
    ${CMAKE_CURRENT_BINARY_DIR}/proxy.dlldata.c
    PROPERTIES COMPILE_DEFINITIONS "WINE_REGISTER_DLL;PROXY_DELEGATION;PROXY_CLSID=CLSID_PSFactoryBuffer;ENTRY_PREFIX=netshell_ps_")

file(GLOB_RECURSE netshell_rc_deps res/*.*)
add_rc_deps(netshell.rc ${netshell_rc_deps})

add_library(netshell MODULE
    ${SOURCE}
    ${PCH_SKIP_SOURCE}
    netshell.rc
    ${CMAKE_CURRENT_BINARY_DIR}/netshell.def)

set_module_type(netshell win32dll UNICODE)
add_idl_reg_scripts(netshell registry netshell_netcon.idl)
target_link_libraries(netshell uuid wine cpprt atl_classes)
add_delay_importlibs(netshell ole32 oleaut32 shlwapi shell32)
add_importlibs(netshell rpcrt4 version iphlpapi gdi32 user32 advapi32 setupapi ws2_32 comctl32 msvcrt kernel32 ntdll)
add_pch(netshell precomp.h "${PCH_SKIP_SOURCE}")
add_cd_file(TARGET netshell DESTINATION reactos/system32 FOR all)
