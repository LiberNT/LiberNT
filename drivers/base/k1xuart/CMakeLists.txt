#
# PROJECT:     LiberNT SpacemiT K1 UART driver
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     SpacemiT K1 UART driver build rules
# COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
#

add_library(k1xuart MODULE k1xuart.c k1xuart.rc)
set_module_type(k1xuart kmdfdriver)
target_link_libraries(k1xuart sercx2stubs ntoskrnl_vista libcntpr ${PSEH_LIB})
add_importlibs(k1xuart ntoskrnl hal)
add_cd_file(TARGET k1xuart DESTINATION reactos/system32/drivers NO_CAB FOR all)
add_driver_inf(k1xuart k1xuart.inf)
