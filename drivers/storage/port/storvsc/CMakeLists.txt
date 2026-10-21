
include_directories(${REACTOS_SOURCE_DIR}/drivers/bus/vmbus)

add_library(storvsc MODULE storvsc.c storvsc.rc)
set_module_type(storvsc kernelmodedriver)
add_importlibs(storvsc vmbus storport ntoskrnl hal)
add_cd_file(TARGET storvsc DESTINATION reactos/system32/drivers NO_CAB FOR all)
add_driver_inf(storvsc storvsc.inf)
