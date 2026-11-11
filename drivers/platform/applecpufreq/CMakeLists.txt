#
# PROJECT:     LiberNT Apple CPU Performance Driver
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     Build the driver of the Apple silicon CPU cluster performance states
# COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
#

add_library(applecpufreq MODULE applecpufreq.c cluster.c governor.c applecpufreq.rc)
set_module_type(applecpufreq kernelmodedriver)
target_link_libraries(applecpufreq fdtdrv)
add_importlibs(applecpufreq ntoskrnl hal)
add_cd_file(TARGET applecpufreq DESTINATION reactos/system32/drivers FOR all)
add_driver_inf(applecpufreq applecpufreq.inf)
