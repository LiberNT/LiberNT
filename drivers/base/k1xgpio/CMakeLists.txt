#
# PROJECT:     LiberNT SpacemiT K1 GPIO driver
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     SpacemiT K1 GPIO driver build rules
# COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
#

add_library(k1xgpio MODULE k1xgpio.c k1xgpio.rc)
set_module_type(k1xgpio kmdfdriver)
target_link_libraries(k1xgpio msgpioclxstub ntoskrnl_vista libcntpr ${PSEH_LIB})
add_importlibs(k1xgpio ntoskrnl hal)
add_cd_file(TARGET k1xgpio DESTINATION reactos/system32/drivers NO_CAB FOR all)
add_driver_inf(k1xgpio k1xgpio.inf)
