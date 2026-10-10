#
# PROJECT:     LiberNT StarFive JH7110 thermal driver
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     StarFive JH7110 thermal driver build rules
# COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
#

add_library(jh7110thermal MODULE jh7110thermal.c jh7110thermal.rc)
set_module_type(jh7110thermal kernelmodedriver)
add_importlibs(jh7110thermal ntoskrnl hal)
add_cd_file(TARGET jh7110thermal DESTINATION reactos/system32/drivers NO_CAB FOR all)
add_driver_inf(jh7110thermal jh7110thermal.inf)
