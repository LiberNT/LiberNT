#
# PROJECT:     LiberNT Tegra USB transceiver driver
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     Tegra USB transceiver driver build rules
# COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
#

add_library(tegrausbphy MODULE tegrausbphy.c tegrausbphy.rc)
set_module_type(tegrausbphy kernelmodedriver)
target_link_libraries(tegrausbphy ${PSEH_LIB})
add_importlibs(tegrausbphy ntoskrnl hal)
add_cd_file(TARGET tegrausbphy DESTINATION reactos/system32/drivers NO_CAB FOR all)
