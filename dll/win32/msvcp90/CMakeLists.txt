set(VC90_ASSEMBLY ${WINARCH}_microsoft.vc90.crt_1fc8b3b9a1e18e3b_9.0.30729.6161_none_deadbeef)

include_directories(BEFORE ${REACTOS_SOURCE_DIR}/sdk/include/wine)

add_definitions(-D_MSVCR_VER=90 -D_MSVCP_VER=90 -D__WINESRC__)

spec2def(msvcp90.dll msvcp90.spec ADD_IMPORTLIB)

list(APPEND SOURCE
    details.c
    exception.c
    ios.c
    locale.c
    math.c
    memory.c
    misc.c
    msvcp_main.c
    string.c
    ${CMAKE_CURRENT_BINARY_DIR}/msvcp90_stubs.c)

add_library(msvcp90 MODULE
    ${SOURCE}
    ${CMAKE_CURRENT_BINARY_DIR}/msvcp90.def)

set_module_type(msvcp90 win32dll ENTRYPOINT DllMain 12)
target_link_libraries(msvcp90 wine)
add_importlibs(msvcp90 msvcr90 kernel32 ntdll)
add_dependencies(msvcp90 psdk)
add_cd_file(TARGET msvcp90 DESTINATION reactos/winsxs/${VC90_ASSEMBLY} FOR all)
