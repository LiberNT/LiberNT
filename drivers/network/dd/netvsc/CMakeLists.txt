
add_definitions(
    -DNDIS60_MINIPORT
    -DNDIS_MINIPORT_DRIVER)

include_directories(${REACTOS_SOURCE_DIR}/drivers/bus/vmbus)

list(APPEND SOURCE
    netvsc.c
    nvsp.c
    netvsc.h)

add_library(netvsc MODULE ${SOURCE} netvsc.rc)
add_pch(netvsc netvsc.h SOURCE)
set_module_type(netvsc kernelmodedriver)
add_importlibs(netvsc vmbus ndis ntoskrnl hal)
add_cd_file(TARGET netvsc DESTINATION reactos/system32/drivers FOR all)
add_driver_inf(netvsc netvsc.inf)
