
spec2def(kd_02_8086.dll kd_02_8086.spec)

list(APPEND SOURCE
    e1000.c
    kdext.c)

add_library(kd_02_8086 MODULE
    ${SOURCE}
    kd_02_8086.rc
    ${CMAKE_CURRENT_BINARY_DIR}/kd_02_8086.def)

set_module_type(kd_02_8086 kerneldll ENTRYPOINT 0)
set_subsystem(kd_02_8086 native)
if(NOT MSVC)
    target_compile_options(kd_02_8086 PRIVATE -fno-builtin)
endif()
add_dependencies(kd_02_8086 psdk xdk)
add_cd_file(TARGET kd_02_8086 DESTINATION reactos/system32 NO_CAB FOR all)
