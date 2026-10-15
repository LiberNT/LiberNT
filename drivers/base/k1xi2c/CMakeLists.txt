#
# PROJECT:     LiberNT SpacemiT K1 I2C driver
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     SpacemiT K1 I2C driver build rules
# COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
#

add_library(k1xi2c MODULE k1xi2c.c k1xi2c.rc)
set_module_type(k1xi2c kmdfdriver)
target_link_libraries(k1xi2c spbcxstubs ntoskrnl_vista libcntpr ${PSEH_LIB})
add_importlibs(k1xi2c ntoskrnl hal)
add_cd_file(TARGET k1xi2c DESTINATION reactos/system32/drivers NO_CAB FOR all)
add_driver_inf(k1xi2c k1xi2c.inf)
