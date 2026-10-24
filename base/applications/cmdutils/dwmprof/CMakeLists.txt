# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Ahmed ARIF <arif193@gmail.com>
add_executable(dwmprof_tool dwmprof.c)
set_target_properties(dwmprof_tool PROPERTIES OUTPUT_NAME "dwmprof" PDB_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_module_type(dwmprof_tool win32cui)
add_importlibs(dwmprof_tool dwmprof msvcrt kernel32)
add_cd_file(TARGET dwmprof_tool DESTINATION reactos/system32 FOR all)
