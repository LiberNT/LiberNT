#
# PROJECT:     LiberNT StarFive JH7110 watchdog driver
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     StarFive JH7110 watchdog driver build rules
# COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
#

add_library(jh7110wdt MODULE jh7110wdt.c jh7110wdt.rc)
set_module_type(jh7110wdt kernelmodedriver)
add_importlibs(jh7110wdt ntoskrnl hal)
add_cd_file(TARGET jh7110wdt DESTINATION reactos/system32/drivers NO_CAB FOR all)
add_driver_inf(jh7110wdt jh7110wdt.inf)
