#
# PROJECT:     LiberNT StarFive JH7110 display miniport
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     Build the JH7110 DC8200/HDMI platform provider over the software GPU engine
# COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
#

include(${REACTOS_SOURCE_DIR}/sdk/lib/drivers/softgpu/softgpu.cmake)
spec2def(jh7110dc.sys ${SOFTGPU_ENGINE_SPEC} ADD_IMPORTLIB)

remove_definitions(-D_WIN32_WINNT=${REACTOS_TARGET_NT})
remove_definitions(-DWINVER=${REACTOS_TARGET_NT})
add_definitions(-D_WIN32_WINNT=0x0601 -DWINVER=0x0601)
add_definitions(-DNTDDI_VERSION=0x06010000)

remove_definitions(-DDXGKDDI_INTERFACE_VERSION=0x1052)
add_definitions(-DDXGKDDI_INTERFACE_VERSION=0x11007)

include_directories(${REACTOS_SOURCE_DIR}/sdk/lib/drivers/jh7110disp)

add_library(jh7110dc MODULE
    platform_jh7110.c
    jh7110dc.rc
    ${CMAKE_CURRENT_BINARY_DIR}/jh7110dc.def)

set_module_type(jh7110dc kernelmodedriver)
target_link_libraries(jh7110dc softgpu_engine displib jh7110disp)
add_importlibs(jh7110dc ntoskrnl hal)
add_cd_file(TARGET jh7110dc DESTINATION reactos/system32/drivers FOR all)
add_driver_inf(jh7110dc jh7110dc.inf)
add_registry_inf(jh7110dc_reg.inf)
