
spec2def(kd_fdt_spacemit_k1x-emac.dll kd_fdt_spacemit_k1x-emac.spec)

list(APPEND SOURCE
    k1xemac.c
    kdext.c)

add_library(kd_fdt_spacemit_k1x-emac MODULE
    ${SOURCE}
    kd_fdt_spacemit_k1x-emac.rc
    ${CMAKE_CURRENT_BINARY_DIR}/kd_fdt_spacemit_k1x-emac.def)

set_module_type(kd_fdt_spacemit_k1x-emac kerneldll ENTRYPOINT 0)
set_subsystem(kd_fdt_spacemit_k1x-emac native)
if(NOT MSVC)
    target_compile_options(kd_fdt_spacemit_k1x-emac PRIVATE -fno-builtin)
endif()
add_dependencies(kd_fdt_spacemit_k1x-emac psdk xdk)
add_cd_file(TARGET kd_fdt_spacemit_k1x-emac DESTINATION reactos/system32 NO_CAB FOR all)
