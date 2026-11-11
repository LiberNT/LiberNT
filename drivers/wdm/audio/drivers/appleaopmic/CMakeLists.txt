#
# PROJECT:     LiberNT Apple AOP Microphone Driver
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     Apple AOP microphone driver build rules
# COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
#

list(APPEND SOURCE
    adapter.cpp
    aopaudio.c
    appleaopmic.cpp
    hardware.c
    miniport.cpp
    stream.cpp
    guid.cpp
    purecall.cpp)

add_library(appleaopmic MODULE
    ${SOURCE}
    appleaopmic.rc)

set_module_type(appleaopmic kernelmodedriver)
target_link_libraries(appleaopmic applertkit fdtdrv ntoskrnl_vista libcntpr ${PSEH_LIB})
add_importlibs(appleaopmic ntoskrnl hal portcls)
add_cd_file(TARGET appleaopmic DESTINATION reactos/system32/drivers FOR all)
add_driver_inf(appleaopmic appleaopmic.inf)
