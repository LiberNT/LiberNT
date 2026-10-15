#
# PROJECT:     LiberNT Bluetooth stack
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     Bluetooth extensible transport miniport build rules
# COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
#

list(APPEND SOURCE
    bthmini.c
    comport.c
    conn.c
    hci.c
    l2cap.c
    radio.c
    rfcomm.c
    sdp.c
    bthmini.h)

add_library(bthmini MODULE ${SOURCE} bthmini.rc)
set_module_type(bthmini kmdfdriver)
target_link_libraries(bthmini wdmsec ntoskrnl_vista libcntpr ${PSEH_LIB})
add_importlibs(bthmini ntoskrnl hal)
add_cd_file(TARGET bthmini DESTINATION reactos/system32/drivers NO_CAB FOR all)
