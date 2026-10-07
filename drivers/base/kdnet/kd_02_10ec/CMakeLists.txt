
spec2def(kd_02_10ec.dll kd_02_10ec.spec)

list(APPEND SOURCE
    kdext.c
    realtek.c)

add_library(kd_02_10ec MODULE
    ${SOURCE}
    kd_02_10ec.rc
    ${CMAKE_CURRENT_BINARY_DIR}/kd_02_10ec.def)

set_module_type(kd_02_10ec kerneldll ENTRYPOINT 0)
set_subsystem(kd_02_10ec native)
if(NOT MSVC)
    target_compile_options(kd_02_10ec PRIVATE -fno-builtin)
endif()
add_dependencies(kd_02_10ec psdk xdk)
add_cd_file(TARGET kd_02_10ec DESTINATION reactos/system32 NO_CAB FOR all)
