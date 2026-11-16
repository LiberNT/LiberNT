#
# PROJECT:     LiberNT MTP device driver
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     MTP device driver build rules
# COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
#

list(APPEND SOURCE
    driver.c
    guids.c
    mtp.c
    objects.c
    transfer.c
    usb.c
    values.c
    wpd.c
    wpdcmd.h
    wpdmtp.h)

add_library(wpdmtp MODULE ${SOURCE} wpdmtp.rc)
set_module_type(wpdmtp kmdfdriver)
target_link_libraries(wpdmtp libcntpr ${PSEH_LIB})
add_importlibs(wpdmtp ntoskrnl hal usbd)
add_cd_file(TARGET wpdmtp DESTINATION reactos/system32/drivers NO_CAB FOR all)
