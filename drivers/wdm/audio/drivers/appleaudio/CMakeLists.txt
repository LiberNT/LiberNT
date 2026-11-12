#
# PROJECT:     LiberNT Apple Silicon Audio Driver
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     Apple silicon audio driver build rules
# COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
#

list(APPEND SOURCE
    adapter.cpp
    appleaudio.cpp
    hardware.c
    jackpolicy.c
    miniport.cpp
    protect.c
    stream.cpp
    tas2764.c
    guid.cpp
    purecall.cpp)

add_library(appleaudio MODULE
    ${SOURCE}
    appleaudio.rc)

set_module_type(appleaudio kernelmodedriver)
target_link_libraries(appleaudio applertkit fdtdrv ntoskrnl_vista libcntpr ${PSEH_LIB})
add_importlibs(appleaudio ntoskrnl hal portcls)
add_cd_file(TARGET appleaudio DESTINATION reactos/system32/drivers FOR all)
add_driver_inf(appleaudio appleaudio.inf)
