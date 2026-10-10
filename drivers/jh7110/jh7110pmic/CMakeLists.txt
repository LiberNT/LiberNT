#
# PROJECT:     LiberNT StarFive JH7110 PMIC driver
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     StarFive JH7110 PMIC driver build rules
# COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
#

add_library(jh7110pmic MODULE jh7110pmic.c jh7110pmic.rc)
set_module_type(jh7110pmic kernelmodedriver)
add_importlibs(jh7110pmic ntoskrnl hal)
add_cd_file(TARGET jh7110pmic DESTINATION reactos/system32/drivers NO_CAB FOR all)
add_driver_inf(jh7110pmic jh7110pmic.inf)
