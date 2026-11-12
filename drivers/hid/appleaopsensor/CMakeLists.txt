#
# PROJECT:     LiberNT Apple AOP Sensor HID Driver
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     Apple AOP sensor HID driver build rules
# COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
#

add_library(appleaopsensor MODULE appleaopsensor.c appleaopsensor.rc)
set_module_type(appleaopsensor kernelmodedriver)
add_importlibs(appleaopsensor hidclass ntoskrnl hal)
add_cd_file(TARGET appleaopsensor DESTINATION reactos/system32/drivers FOR all)
add_driver_inf(appleaopsensor appleaopsensor.inf)
