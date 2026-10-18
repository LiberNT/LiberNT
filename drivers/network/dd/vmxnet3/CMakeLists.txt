
add_definitions(
    -DNDIS620_MINIPORT
    -DNDIS_MINIPORT_DRIVER
    -DNDIS_LEGACY_DRIVER=1
    -DNDIS_LEGACY_MINIPORT=1
    -DNTDDI_VERSION=0x06010000)

list(APPEND SOURCE
    vmx3.c
    vmx3.h)

add_library(vmx3 MODULE ${SOURCE} vmx3.rc)
add_pch(vmx3 vmx3.h SOURCE)
set_module_type(vmx3 kernelmodedriver)
add_importlibs(vmx3 ndis ntoskrnl hal)
add_cd_file(TARGET vmx3 DESTINATION reactos/system32/drivers NO_CAB FOR all)
add_driver_inf(vmx3 netvmx3.inf)
