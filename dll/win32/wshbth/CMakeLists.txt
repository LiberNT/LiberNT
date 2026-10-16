#
# PROJECT:     LiberNT Windows Sockets Helper for Bluetooth
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     Build rules
# COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
#

spec2def(wshbth.dll wshbth.spec)

list(APPEND SOURCE
    nsp.c
    wshbth.c
    wshbth.rc
    ${CMAKE_CURRENT_BINARY_DIR}/wshbth.def)

add_library(wshbth MODULE ${SOURCE})
set_module_type(wshbth win32dll UNICODE)
add_importlibs(wshbth bluetoothapis ws2_32 msvcrt kernel32 ntdll)
add_cd_file(TARGET wshbth DESTINATION reactos/system32 FOR all)
