
include_directories(${REACTOS_SOURCE_DIR}/drivers/bus/vmbus)

add_library(hyperkbd MODULE hyperkbd.c hyperkbd.rc)
set_module_type(hyperkbd kernelmodedriver)
add_importlibs(hyperkbd vmbus ntoskrnl hal)
add_cd_file(TARGET hyperkbd DESTINATION reactos/system32/drivers FOR all)
add_driver_inf(hyperkbd hyperkbd.inf)
