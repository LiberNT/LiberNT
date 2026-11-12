#
# PROJECT:     LiberNT Tegra GPIO driver
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     Tegra GPIO driver build rules
# COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
#

add_library(tegragpio MODULE tegragpio.c tegragpio.rc)
set_module_type(tegragpio kmdfdriver)
target_link_libraries(tegragpio msgpioclxstub ntoskrnl_vista libcntpr ${PSEH_LIB})
add_importlibs(tegragpio ntoskrnl hal)
add_cd_file(TARGET tegragpio DESTINATION reactos/system32/drivers NO_CAB FOR all)
add_driver_inf(tegragpio tegragpio.inf)
