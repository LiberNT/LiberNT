#
# PROJECT:     LiberNT Apple AOP Driver
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     Apple Always-On Processor driver build rules
# COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
#

add_library(appleaop MODULE appleaop.c afk.c sensors.c appleaop.rc)
set_module_type(appleaop kernelmodedriver)
target_link_libraries(appleaop applertkit fdtdrv)
add_importlibs(appleaop ntoskrnl hal)
add_cd_file(TARGET appleaop DESTINATION reactos/system32/drivers FOR all)
add_driver_inf(appleaop appleaop.inf)
