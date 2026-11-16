#
# PROJECT:     LiberNT SpacemiT K1 Audio Driver
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     SpacemiT K1 audio driver build rules
# COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
#

list(APPEND SOURCE
    hardware.cpp
    miniport.cpp
    k1xaudio.cpp
    stream.cpp
    guid.cpp
    purecall.cpp)

add_library(k1xaudio MODULE
    ${SOURCE}
    k1xaudio.rc)

set_module_type(k1xaudio kernelmodedriver)
target_link_libraries(k1xaudio libcntpr ${PSEH_LIB})
add_importlibs(k1xaudio ntoskrnl hal portcls)
add_cd_file(TARGET k1xaudio DESTINATION reactos/system32/drivers FOR all)
add_driver_inf(k1xaudio k1xaudio.inf)
