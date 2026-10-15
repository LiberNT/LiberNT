#
# PROJECT:     LiberNT Ralink RT2800 USB Native 802.11 Miniport
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     RT2870/RT2770 USB Native Wi-Fi miniport build rules
# COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
#

add_definitions(
    -DNDIS620_MINIPORT
    -DNDIS_MINIPORT_DRIVER
    -DNDIS_LEGACY_DRIVER=1
    -DNTDDI_VERSION=0x06010000)

list(APPEND SOURCE
    rt2800usb.c
    usb.c
    hw.c
    mlme.c
    oid.c
    rt2800usb.h)

add_library(rt2800usb MODULE ${SOURCE} rt2800usb.rc)
set_module_type(rt2800usb kernelmodedriver)
add_importlibs(rt2800usb ndis ntoskrnl hal usbd)
add_cd_file(TARGET rt2800usb DESTINATION reactos/system32/drivers NO_CAB FOR all)
add_driver_inf(rt2800usb netrt2800usb.inf)

add_cd_file(FILE ${CMAKE_CURRENT_SOURCE_DIR}/fw/rt2870.bin DESTINATION reactos/system32/drivers/ralink NO_CAB FOR all)
