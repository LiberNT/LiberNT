#
# PROJECT:     LiberNT Virtual WiFi Bus Driver
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     Virtual WiFi bus upper filter build rules
# COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
#

add_library(vwifibus MODULE
    vwifibus.c
    vwifibus.rc)

set_module_type(vwifibus kernelmodedriver)
add_importlibs(vwifibus ntoskrnl hal)
add_cd_file(TARGET vwifibus DESTINATION reactos/system32/drivers NO_CAB FOR all)
add_driver_inf(vwifibus netvwifibus.inf)
