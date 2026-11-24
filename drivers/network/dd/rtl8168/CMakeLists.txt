#
# PROJECT:     LiberNT Realtek RTL8168/8111 Driver
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     RTL8168/8111H NDIS 6.30 miniport build rules
# COPYRIGHT:   Copyright 2026 jewchud <kz2hb7hpvp@privaterelay.appleid.com>
#

add_definitions(
    -DNDIS620_MINIPORT
    -DNDIS_MINIPORT_DRIVER
    -DNDIS_LEGACY_DRIVER=1
    -DNDIS_LEGACY_MINIPORT=1)

list(APPEND SOURCE
    rtl8168.c
    rtl8168.h)

add_library(rtl8168 MODULE ${SOURCE} rtl8168.rc)
add_pch(rtl8168 rtl8168.h SOURCE)
set_module_type(rtl8168 kernelmodedriver)
add_importlibs(rtl8168 ndis ntoskrnl hal)
add_cd_file(TARGET rtl8168 DESTINATION reactos/system32/drivers FOR all)
add_driver_inf(rtl8168 netrtl8168.inf)

add_cd_file(FILE ${CMAKE_CURRENT_SOURCE_DIR}/fw/rtl8168h-2.fw DESTINATION reactos/system32/drivers/realtek NO_CAB FOR all)
add_cd_file(FILE ${CMAKE_CURRENT_SOURCE_DIR}/fw/LICENCE.rtlwifi_firmware.txt DESTINATION reactos/3rdParty NO_CAB FOR all)
