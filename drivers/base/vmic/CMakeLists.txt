
include_directories(${REACTOS_SOURCE_DIR}/drivers/bus/vmbus)

add_library(vmic MODULE vmic.c vmic.rc)
set_module_type(vmic kernelmodedriver)
add_importlibs(vmic vmbus ntoskrnl hal)
add_cd_file(TARGET vmic DESTINATION reactos/system32/drivers FOR all)
add_driver_inf(vmic vmic.inf)
