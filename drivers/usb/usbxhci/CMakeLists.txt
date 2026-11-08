list(APPEND SOURCE
    usbxhci.c
    usbxhci.h
    tt.c)

if(ARCH STREQUAL "arm64")
    list(APPEND SOURCE arch/arm64/xhciarch.c)
else()
    list(APPEND SOURCE arch/generic/xhciarch.c)
endif()

add_library(usbxhci MODULE
    ${SOURCE}
    guid.c
    usbxhci.rc)

set_module_type(usbxhci kernelmodedriver)
add_importlibs(usbxhci usbport usbd hal ntoskrnl)
add_pch(usbxhci usbxhci.h SOURCE)
add_cd_file(TARGET usbxhci DESTINATION reactos/system32/drivers NO_CAB FOR all)
