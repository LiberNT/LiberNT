
include_directories(${REACTOS_SOURCE_DIR}/drivers/bus/vmbus)

add_library(hypermou MODULE hypermou.c hypermou.rc)
set_module_type(hypermou kernelmodedriver)
add_importlibs(hypermou vmbus ntoskrnl hal)
add_cd_file(TARGET hypermou DESTINATION reactos/system32/drivers FOR all)
add_driver_inf(hypermou hypermou.inf)
