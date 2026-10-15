set(VC90_ASSEMBLY ${WINARCH}_microsoft.vc90.crt_1fc8b3b9a1e18e3b_9.0.30729.6161_none_deadbeef)

include_directories(BEFORE ${REACTOS_SOURCE_DIR}/sdk/include/wine)

spec2def(msvcm90.dll msvcm90.spec)

list(APPEND SOURCE
    msvcm_main.c
    ${CMAKE_CURRENT_BINARY_DIR}/msvcm90_stubs.c)

add_library(msvcm90 MODULE
    ${SOURCE}
    ${CMAKE_CURRENT_BINARY_DIR}/msvcm90.def)

set_module_type(msvcm90 win32dll ENTRYPOINT 0)
target_link_libraries(msvcm90 wine)
add_importlibs(msvcm90 kernel32 ntdll)
add_dependencies(msvcm90 psdk)
add_cd_file(TARGET msvcm90 DESTINATION reactos/winsxs/${VC90_ASSEMBLY} FOR all)
