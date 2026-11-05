# PROJECT:  LiberNT Remote Display Miniport
# LICENSE:  GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:  Build file for rdpvdd.sys, the WDDM display-only miniport of the remote display

remove_definitions(-D_WIN32_WINNT=${REACTOS_TARGET_NT})
remove_definitions(-DWINVER=${REACTOS_TARGET_NT})
add_definitions(-D_WIN32_WINNT=0x0602 -DWINVER=0x0602)
add_definitions(-DNTDDI_VERSION=0x06020000)
remove_definitions(-DDXGKDDI_INTERFACE_VERSION=0x1052)
add_definitions(-DDXGKDDI_INTERFACE_VERSION=0x11007)

add_library(rdpvdd MODULE rdpvdd.c rdpvdd.rc)
set_module_type(rdpvdd kernelmodedriver)
target_link_libraries(rdpvdd displib)
add_importlibs(rdpvdd ntoskrnl hal)
add_cd_file(TARGET rdpvdd DESTINATION reactos/system32/drivers FOR all)
add_driver_inf(rdpvdd rdpvdd.inf)
