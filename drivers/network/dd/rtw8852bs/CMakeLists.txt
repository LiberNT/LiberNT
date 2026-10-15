#
# PROJECT:     LiberNT Realtek RTL8852BS Native 802.11 Miniport
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     RTL8852BS SDIO Native Wi-Fi miniport build rules
# COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
#

add_definitions(
    -DNDIS620_MINIPORT
    -DNDIS_MINIPORT_DRIVER
    -DNDIS_LEGACY_DRIVER=1
    -DNTDDI_VERSION=0x06010000)

list(APPEND SOURCE
    rtw8852bs.c
    sdio.c
    mac.c
    fw.c
    macinit.c
    rx.c
    phy.c
    rfk.c
    tx.c
    role.c
    mlme.c
    oid.c
    txpwr.c
    rtw8852bs.h
    reg.h
    phyreg.h)

include_directories(${REACTOS_SOURCE_DIR}/sdk/lib/drivers/rsna_supplicant)

add_library(rtw8852bs MODULE ${SOURCE} rtw8852bs.rc)
set_module_type(rtw8852bs kernelmodedriver)
target_link_libraries(rtw8852bs sdbuslib rsna_supplicant)
add_importlibs(rtw8852bs ndis ntoskrnl hal)
add_cd_file(TARGET rtw8852bs DESTINATION reactos/system32/drivers NO_CAB FOR all)
add_driver_inf(rtw8852bs netrtw8852bs.inf)

add_cd_file(FILE ${CMAKE_CURRENT_SOURCE_DIR}/fw/rtw8852b_fw-2.bin DESTINATION reactos/system32/drivers/realtek NO_CAB FOR all)
add_cd_file(FILE ${CMAKE_CURRENT_SOURCE_DIR}/fw/LICENCE.rtlwifi_firmware.txt DESTINATION reactos/3rdParty NO_CAB FOR all)
