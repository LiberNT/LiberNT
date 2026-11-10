
list(APPEND SOURCE
    ccid.c
    usbccid.c
    usbccid.h)

add_library(usbccid MODULE
    ${SOURCE}
    usbccid.rc)

set_module_type(usbccid kernelmodedriver)
add_importlibs(usbccid smclib usbd ntoskrnl hal)
add_cd_file(TARGET usbccid DESTINATION reactos/system32/drivers NO_CAB FOR all)
add_driver_inf(usbccid usbccid.inf)
