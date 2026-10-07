
spec2def(kd_8003_1de4.dll kd_8003_1de4.spec)

list(APPEND SOURCE
    kdext.c
    rp1gem.c)

add_library(kd_8003_1de4 MODULE
    ${SOURCE}
    kd_8003_1de4.rc
    ${CMAKE_CURRENT_BINARY_DIR}/kd_8003_1de4.def)

set_module_type(kd_8003_1de4 kerneldll ENTRYPOINT 0)
set_subsystem(kd_8003_1de4 native)
if(NOT MSVC)
    target_compile_options(kd_8003_1de4 PRIVATE -fno-builtin)
endif()
add_dependencies(kd_8003_1de4 psdk xdk)
add_cd_file(TARGET kd_8003_1de4 DESTINATION reactos/system32 NO_CAB FOR all)
