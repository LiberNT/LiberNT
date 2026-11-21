#
# PROJECT:     LiberNT SpacemiT K1 Ethernet Driver
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     SpacemiT K1 EMAC miniport build rules
# COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
#

add_definitions(
    -DNDIS620_MINIPORT
    -DNDIS_MINIPORT_DRIVER
    -DNDIS_LEGACY_DRIVER=1
    -DNDIS_LEGACY_MINIPORT=1)

list(APPEND SOURCE
    k1xemac.c
    k1xemac.h)

add_library(k1xemac MODULE ${SOURCE} k1xemac.rc)
add_pch(k1xemac k1xemac.h SOURCE)
set_module_type(k1xemac kernelmodedriver)
add_importlibs(k1xemac ndis ntoskrnl hal)
add_cd_file(TARGET k1xemac DESTINATION reactos/system32/drivers NO_CAB FOR all)
add_driver_inf(k1xemac netk1xemac.inf)
