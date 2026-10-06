# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Ahmed ARIF <arif.ing@outlook.com>

add_rc_deps(msinfo32.rc
    ${CMAKE_CURRENT_SOURCE_DIR}/resource.h
    ${CMAKE_CURRENT_SOURCE_DIR}/lang/en-US.rc)
add_executable(msinfo32 msinfo32.c msinfo32.rc)
set_module_type(msinfo32 win32gui UNICODE)
add_importlibs(msinfo32 comctl32 shell32 shlwapi advapi32 user32 msvcrt kernel32)
add_cd_file(TARGET msinfo32 DESTINATION reactos/system32 FOR all)
