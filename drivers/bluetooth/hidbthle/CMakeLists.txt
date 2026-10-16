#
# PROJECT:     LiberNT Bluetooth LE HID miniport
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     Bluetooth LE HID miniport build rules
# COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
#

include_directories(${REACTOS_SOURCE_DIR}/drivers/bluetooth/include)

add_library(hidbthle MODULE hidbthle.c hidbthle.h hidbthle.rc)
set_module_type(hidbthle kernelmodedriver)
add_importlibs(hidbthle hidclass ntoskrnl hal)
add_cd_file(TARGET hidbthle DESTINATION reactos/system32/drivers FOR all)
add_driver_inf(hidbthle hidbthle.inf)
