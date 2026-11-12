list(APPEND SOURCE
    guid.c
    pipe.c
    pnp.c
    setting.c
    winusb.c
    winusb.h)

add_library(winusb_sys MODULE
    ${SOURCE}
    winusb.rc)

set_target_properties(winusb_sys PROPERTIES OUTPUT_NAME "winusb")
set_module_type(winusb_sys kernelmodedriver)
add_importlibs(winusb_sys ntoskrnl hal usbd)
add_cd_file(TARGET winusb_sys DESTINATION reactos/system32/drivers NO_CAB FOR all)
add_driver_inf(winusb_sys winusb.inf)
