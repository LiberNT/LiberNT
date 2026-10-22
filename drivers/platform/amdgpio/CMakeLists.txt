#
# PROJECT:     LiberNT AMD GPIO driver
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     AMD GPIO driver build rules
# COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
#

add_library(amdgpio MODULE amdgpio.c amdgpio.rc)
set_module_type(amdgpio kmdfdriver)
target_link_libraries(amdgpio msgpioclxstub ntoskrnl_vista libcntpr ${PSEH_LIB})
add_importlibs(amdgpio ntoskrnl hal)
add_cd_file(TARGET amdgpio DESTINATION reactos/system32/drivers FOR all)
add_driver_inf(amdgpio amdgpio.inf)
