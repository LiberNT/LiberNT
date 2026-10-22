#
# PROJECT:     LiberNT Arm PL061 GPIO driver
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     Arm PL061 GPIO driver build rules
# COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
#

add_library(pl061gpio MODULE pl061gpio.c pl061gpio.rc)
set_module_type(pl061gpio kmdfdriver)
target_link_libraries(pl061gpio msgpioclxstub ntoskrnl_vista libcntpr ${PSEH_LIB})
add_importlibs(pl061gpio ntoskrnl hal)
add_cd_file(TARGET pl061gpio DESTINATION reactos/system32/drivers NO_CAB FOR all)
add_driver_inf(pl061gpio pl061gpio.inf)
