#
# PROJECT:     LiberNT Bluetooth HID miniport
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     Bluetooth HID miniport build rules
# COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
#

include_directories(${REACTOS_SOURCE_DIR}/drivers/bluetooth/include)

add_library(hidbth MODULE hidbth.c hidbth.h hidbth.rc)
set_module_type(hidbth kernelmodedriver)
add_importlibs(hidbth hidclass ntoskrnl hal)
add_cd_file(TARGET hidbth DESTINATION reactos/system32/drivers FOR all)
add_driver_inf(hidbth hidbth.inf)
