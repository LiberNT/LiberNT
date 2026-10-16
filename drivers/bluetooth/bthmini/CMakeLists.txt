#
# PROJECT:     LiberNT Bluetooth stack
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     Bluetooth extensible transport miniport build rules
# COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
#

include_directories(
    ${REACTOS_SOURCE_DIR}/drivers/bluetooth/include
    ${REACTOS_SOURCE_DIR}/sdk/lib/drivers/rsna_supplicant)

list(APPEND SOURCE
    bthmini.c
    brb.c
    comport.c
    conn.c
    enum.c
    gatt.c
    le.c
    hci.c
    l2cap.c
    radio.c
    rfcomm.c
    sdp.c
    sdpclient.c
    smp.c
    tdi.c
    bthmini.h)

add_library(bthmini MODULE ${SOURCE} bthmini.rc)
set_module_type(bthmini kmdfdriver)
target_link_libraries(bthmini wdmsec ntoskrnl_vista libcntpr rsna_supplicant ${PSEH_LIB})
add_importlibs(bthmini ntoskrnl hal cng)
add_cd_file(TARGET bthmini DESTINATION reactos/system32/drivers NO_CAB FOR all)
