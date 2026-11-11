#
# PROJECT:     LiberNT Apple ISP Camera Driver
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     Build the Apple silicon ISP camera AVStream driver
# COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
#

list(APPEND SOURCE
    appleisp.c
    command.c
    firmware.c
    guid.c
    stream.c)

add_library(appleisp MODULE
    ${SOURCE}
    appleisp.rc)

set_module_type(appleisp kernelmodedriver)
target_link_libraries(appleisp applertkit fdtdrv libcntpr)
add_importlibs(appleisp ks ntoskrnl hal)
add_cd_file(TARGET appleisp DESTINATION reactos/system32/drivers FOR all)
add_driver_inf(appleisp appleisp.inf)
