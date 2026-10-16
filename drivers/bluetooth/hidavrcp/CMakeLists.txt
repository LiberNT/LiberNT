#
# PROJECT:     LiberNT Bluetooth AVRCP HID miniport
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     Bluetooth AVRCP HID miniport build rules
# COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
#

include_directories(${REACTOS_SOURCE_DIR}/drivers/bluetooth/include)

add_library(hidavrcp MODULE hidavrcp.c hidavrcp.rc)
set_module_type(hidavrcp kernelmodedriver)
add_importlibs(hidavrcp hidclass ntoskrnl hal)
add_cd_file(TARGET hidavrcp DESTINATION reactos/system32/drivers FOR all)
add_driver_inf(hidavrcp hidavrcp.inf)
