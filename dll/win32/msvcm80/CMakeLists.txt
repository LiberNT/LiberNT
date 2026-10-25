set(VC80_ASSEMBLY ${WINARCH}_microsoft.vc80.crt_1fc8b3b9a1e18e3b_8.0.50727.9680_none_deadbeef)

include_directories(BEFORE ${REACTOS_SOURCE_DIR}/sdk/include/wine)

spec2def(msvcm80.dll msvcm80.spec)

list(APPEND SOURCE
    ${REACTOS_SOURCE_DIR}/dll/win32/msvcm90/msvcm_main.c
    ${CMAKE_CURRENT_BINARY_DIR}/msvcm80_stubs.c)

add_library(msvcm80 MODULE
    ${SOURCE}
    ${CMAKE_CURRENT_BINARY_DIR}/msvcm80.def)

set_module_type(msvcm80 win32dll ENTRYPOINT 0)
target_link_libraries(msvcm80 wine)
add_importlibs(msvcm80 kernel32 ntdll)
add_dependencies(msvcm80 psdk)
add_cd_file(TARGET msvcm80 DESTINATION reactos/winsxs/${VC80_ASSEMBLY} FOR all)
