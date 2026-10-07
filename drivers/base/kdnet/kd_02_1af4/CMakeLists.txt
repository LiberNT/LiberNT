
spec2def(kd_02_1af4.dll kd_02_1af4.spec)

list(APPEND SOURCE
    kdext.c
    virtio.c)

add_library(kd_02_1af4 MODULE
    ${SOURCE}
    kd_02_1af4.rc
    ${CMAKE_CURRENT_BINARY_DIR}/kd_02_1af4.def)

set_module_type(kd_02_1af4 kerneldll ENTRYPOINT 0)
set_subsystem(kd_02_1af4 native)
if(NOT MSVC)
    target_compile_options(kd_02_1af4 PRIVATE -fno-builtin)
endif()
add_dependencies(kd_02_1af4 psdk xdk)
add_cd_file(TARGET kd_02_1af4 DESTINATION reactos/system32 NO_CAB FOR all)
