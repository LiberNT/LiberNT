#
# PROJECT:     LiberNT ACPI Generic Button Driver
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     ACPI generic button driver build rules
# COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
#

add_library(genbtn MODULE genbtn.c genbtn.rc)
set_module_type(genbtn kernelmodedriver)
add_importlibs(genbtn ntoskrnl hal)
add_cd_file(TARGET genbtn DESTINATION reactos/system32/drivers FOR all)
add_driver_inf(genbtn genbtn.inf)
