#
# PROJECT:     LiberNT StarFive JH7110 Ethernet Driver
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     StarFive JH7110 GMAC miniport build rules
# COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
#

add_definitions(
    -DNDIS620_MINIPORT
    -DNDIS_MINIPORT_DRIVER
    -DNDIS_LEGACY_DRIVER=1
    -DNDIS_LEGACY_MINIPORT=1
    -DNTDDI_VERSION=0x06010000)

list(APPEND SOURCE
    jh7110gmac.c
    jh7110gmac.h)

add_library(jh7110gmac MODULE ${SOURCE} jh7110gmac.rc)
add_pch(jh7110gmac jh7110gmac.h SOURCE)
set_module_type(jh7110gmac kernelmodedriver)
add_importlibs(jh7110gmac ndis ntoskrnl hal)
add_cd_file(TARGET jh7110gmac DESTINATION reactos/system32/drivers NO_CAB FOR all)
add_driver_inf(jh7110gmac netjh7110gmac.inf)
