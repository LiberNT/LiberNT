#
# PROJECT:     LiberNT Bluetooth A2DP audio driver
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     Bluetooth A2DP audio driver build rules
# COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
#

include_directories(${REACTOS_SOURCE_DIR}/drivers/bluetooth/include)

list(APPEND SOURCE
    a2dp.c
    sbc.c
    adapter.cpp
    miniport.cpp
    guid.cpp
    purecall.cpp)

add_library(btha2dp MODULE ${SOURCE} btha2dp.rc)
set_module_type(btha2dp kernelmodedriver)
target_link_libraries(btha2dp ntoskrnl_vista libcntpr ${PSEH_LIB})
add_importlibs(btha2dp portcls ntoskrnl hal)
add_cd_file(TARGET btha2dp DESTINATION reactos/system32/drivers FOR all)
add_driver_inf(btha2dp btha2dp.inf)
