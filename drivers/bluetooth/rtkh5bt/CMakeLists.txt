#
# PROJECT:     LiberNT Realtek Bluetooth UART transport
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     Realtek three-wire UART Bluetooth transport build rules
# COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
#

set(RTKH5BT_FIRMWARE_DIR "" CACHE PATH "Directory holding rtl8852bs_fw.bin and rtl8852bs_config.bin")

list(APPEND SOURCE
    rtkh5bt.c
    h5.c
    rtkfw.c
    rtkh5bt.h)

add_library(rtkh5bt MODULE ${SOURCE} rtkh5bt.rc)
set_module_type(rtkh5bt kmdfdriver)
target_link_libraries(rtkh5bt ntoskrnl_vista libcntpr ${PSEH_LIB})
add_importlibs(rtkh5bt ntoskrnl hal)
add_cd_file(TARGET rtkh5bt DESTINATION reactos/system32/drivers NO_CAB FOR all)
add_driver_inf(rtkh5bt rtkh5bt.inf)

if(RTKH5BT_FIRMWARE_DIR)
    add_cd_file(FILE ${RTKH5BT_FIRMWARE_DIR}/rtl8852bs_fw.bin DESTINATION reactos/system32/drivers/realtek NO_CAB FOR all)
    add_cd_file(FILE ${RTKH5BT_FIRMWARE_DIR}/rtl8852bs_config.bin DESTINATION reactos/system32/drivers/realtek NO_CAB FOR all)
endif()
