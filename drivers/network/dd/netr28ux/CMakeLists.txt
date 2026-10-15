#
# PROJECT:     LiberNT MediaTek RT2770 USB Wi-Fi driver package
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     Prebuilt MediaTek netr28ux 5.1.38.0 x64 driver installed in-box
# COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
#

add_cd_file(FILE
    ${CMAKE_CURRENT_SOURCE_DIR}/netr28ux.sys
    ${CMAKE_CURRENT_SOURCE_DIR}/FW_7662.bin
    ${CMAKE_CURRENT_SOURCE_DIR}/Patch_7662.bin
    DESTINATION reactos/system32/drivers FOR all)
add_cd_file(FILE
    ${CMAKE_CURRENT_SOURCE_DIR}/RaCoInstx.dll
    ${CMAKE_CURRENT_SOURCE_DIR}/RaCoInst.dat
    DESTINATION reactos/system32 FOR all)
add_driver_inf(netr28ux netr28ux.inf)
