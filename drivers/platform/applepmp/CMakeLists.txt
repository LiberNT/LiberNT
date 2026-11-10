#
# PROJECT:     LiberNT Apple Power Management Processor Driver
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     Build the driver of the Apple silicon power management processor
# COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
#

add_library(applepmp MODULE applepmp.c applepmp.rc)
set_module_type(applepmp kernelmodedriver)
target_link_libraries(applepmp applertkit fdtdrv)
add_importlibs(applepmp ntoskrnl hal)
add_cd_file(TARGET applepmp DESTINATION reactos/system32/drivers FOR all)
add_driver_inf(applepmp applepmp.inf)
