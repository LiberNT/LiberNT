# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Ahmed ARIF <arif.ing@outlook.com>

add_library(pvrkmt STATIC pvrkmt.c)
add_dependencies(pvrkmt psdk)
target_include_directories(pvrkmt PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${REACTOS_SOURCE_DIR}/submodules/mesa/include)
