#
# PROJECT:     LiberNT Apple Graphics Driver
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     Build the driver of the Apple silicon graphics processor
# COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
#

add_library(appleagx MODULE appleagx.c agxengine.c appleagx.rc)
set_module_type(appleagx kernelmodedriver)
target_link_libraries(appleagx applertkit fdtdrv ${PSEH_LIB})
add_importlibs(appleagx ntoskrnl hal)
add_cd_file(TARGET appleagx DESTINATION reactos/system32/drivers FOR all)
add_driver_inf(appleagx appleagx.inf)
