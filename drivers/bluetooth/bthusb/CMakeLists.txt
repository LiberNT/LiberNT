#
# PROJECT:     LiberNT Bluetooth USB transport
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     Bluetooth HCI USB transport build rules
# COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
#

include_directories(${REACTOS_SOURCE_DIR}/sdk/lib/drivers/rtkbtfw)

list(APPEND SOURCE
    bthusb.c
    usb.c
    bthusb.h)

add_library(bthusb MODULE ${SOURCE} bthusb.rc)
set_module_type(bthusb kmdfdriver)
target_link_libraries(bthusb rtkbtfw ntoskrnl_vista libcntpr ${PSEH_LIB})
add_importlibs(bthusb ntoskrnl hal usbd)
add_cd_file(TARGET bthusb DESTINATION reactos/system32/drivers NO_CAB FOR all)
