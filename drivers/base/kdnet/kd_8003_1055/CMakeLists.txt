
spec2def(kd_8003_1055.dll kd_8003_1055.spec)

list(APPEND SOURCE
    dwc2.c
    kdext.c
    lan95xx.c
    usb.c)

add_library(kd_8003_1055 MODULE
    ${SOURCE}
    kd_8003_1055.rc
    ${CMAKE_CURRENT_BINARY_DIR}/kd_8003_1055.def)

set_module_type(kd_8003_1055 kerneldll ENTRYPOINT 0)
set_subsystem(kd_8003_1055 native)
if(NOT MSVC)
    target_compile_options(kd_8003_1055 PRIVATE -fno-builtin)
endif()
add_dependencies(kd_8003_1055 psdk xdk)
add_cd_file(TARGET kd_8003_1055 DESTINATION reactos/system32 NO_CAB FOR all)
