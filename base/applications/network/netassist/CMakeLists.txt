# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Ahmed ARIF <arif.ing@outlook.com>

add_rc_deps(netassist.rc
    ${CMAKE_CURRENT_SOURCE_DIR}/resource.h
    ${CMAKE_CURRENT_SOURCE_DIR}/lang/en-US.rc
    ${CMAKE_CURRENT_SOURCE_DIR}/../../control11/res/cat_network.ico)
add_executable(netassist
    netassist.c
    netassist.rc)
set_module_type(netassist win32gui UNICODE)
add_importlibs(netassist advapi32 user32 gdi32 shell32 comctl32 msvcrt kernel32)
add_cd_file(TARGET netassist DESTINATION reactos/system32 FOR all)
