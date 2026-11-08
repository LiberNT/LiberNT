#
# PROJECT:     LiberNT Broadcom PCIe Bluetooth transport
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     Broadcom PCIe Bluetooth transport build rules
# COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
#

list(APPEND SOURCE
    bcmpcibt.c
    boot.c
    proto.c
    rings.c
    bcmpcibt.h
    proto.h)

add_library(bcmpcibt MODULE ${SOURCE} bcmpcibt.rc)
set_module_type(bcmpcibt kmdfdriver)
target_link_libraries(bcmpcibt applertkit fdtdrv wdmguid ntoskrnl_vista libcntpr ${PSEH_LIB})
add_importlibs(bcmpcibt ntoskrnl hal)
add_cd_file(TARGET bcmpcibt DESTINATION reactos/system32/drivers NO_CAB FOR all)
add_driver_inf(bcmpcibt bcmpcibt.inf)
