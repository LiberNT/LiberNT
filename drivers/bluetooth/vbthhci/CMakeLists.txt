#
# PROJECT:     LiberNT Virtual Bluetooth Controller
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     Software-only Bluetooth HCI controller build rules
# COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
#

add_library(vbthhci MODULE vbthhci.c vbthhci.rc)
set_module_type(vbthhci kmdfdriver)
target_link_libraries(vbthhci ntoskrnl_vista libcntpr ${PSEH_LIB})
add_importlibs(vbthhci ntoskrnl hal)
add_cd_file(TARGET vbthhci DESTINATION reactos/system32/drivers NO_CAB FOR all)
add_driver_inf(vbthhci vbthhci.inf)
