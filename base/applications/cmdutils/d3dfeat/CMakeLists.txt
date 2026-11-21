# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Ahmed ARIF <arif193@gmail.com>
remove_definitions(-DD3D_UMD_INTERFACE_VERSION=${REACTOS_WDDM_EFFECTIVE_UMD_INTERFACE_VERSION})
add_definitions(-DD3D_UMD_INTERFACE_VERSION=0x11000)

add_executable(d3dfeat d3dfeat.c)
set_module_type(d3dfeat win32cui)
target_link_libraries(d3dfeat dxguid uuid)
add_importlibs(d3dfeat d3d11 d3d12 dxgi d3dcompiler_47 user32 gdi32 msvcrt kernel32)
add_dependencies(d3dfeat d3d_idl_headers)
add_cd_file(TARGET d3dfeat DESTINATION reactos/system32 FOR all)
