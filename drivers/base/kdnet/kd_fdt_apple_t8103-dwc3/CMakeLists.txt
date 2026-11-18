
include_directories(${REACTOS_SOURCE_DIR}/drivers/base/kdnet/kd_usbnet)

spec2def(kd_fdt_apple_t8103-dwc3.dll ../kd_usbnet/kd_usbnet.spec)

add_library(kd_fdt_apple_t8103-dwc3 MODULE
    glue.c
    kd_fdt_apple_t8103-dwc3.rc
    ${CMAKE_CURRENT_BINARY_DIR}/kd_fdt_apple_t8103-dwc3.def)

set_module_type(kd_fdt_apple_t8103-dwc3 kerneldll ENTRYPOINT 0)
set_subsystem(kd_fdt_apple_t8103-dwc3 native)
target_link_libraries(kd_fdt_apple_t8103-dwc3 kdusbnetcore applertkitcore fdtnode libcntpr)
if(NOT MSVC)
    target_compile_options(kd_fdt_apple_t8103-dwc3 PRIVATE -fno-builtin)
endif()
add_dependencies(kd_fdt_apple_t8103-dwc3 psdk xdk)
add_cd_file(TARGET kd_fdt_apple_t8103-dwc3 DESTINATION reactos/system32 NO_CAB FOR all)
