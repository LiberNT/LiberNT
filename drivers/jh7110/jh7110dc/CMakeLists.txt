#
# PROJECT:     LiberNT StarFive JH7110 display miniport
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     Build the JH7110 DC8200/HDMI platform provider over the software GPU engine
# COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
#

set(SOFTGPU_SOURCE_DIR ${REACTOS_SOURCE_DIR}/drivers/directx/softgpu)

spec2def(jh7110dc.sys ../../directx/softgpu/softgpu.spec ADD_IMPORTLIB)

remove_definitions(-D_WIN32_WINNT=${REACTOS_TARGET_NT})
remove_definitions(-DWINVER=${REACTOS_TARGET_NT})
add_definitions(-D_WIN32_WINNT=0x0601 -DWINVER=0x0601)
add_definitions(-DNTDDI_VERSION=0x06010000)

remove_definitions(-DDXGKDDI_INTERFACE_VERSION=0x1052)
add_definitions(-DDXGKDDI_INTERFACE_VERSION=0x11007)

include_directories(${SOFTGPU_SOURCE_DIR} ${REACTOS_SOURCE_DIR}/sdk/lib/drivers/jh7110disp)

list(APPEND SOURCE
    ${SOFTGPU_SOURCE_DIR}/softgpu.c
    ${SOFTGPU_SOURCE_DIR}/platform_common.c
    ${SOFTGPU_SOURCE_DIR}/vidpn.c
    ${SOFTGPU_SOURCE_DIR}/pointer.c
    ${SOFTGPU_SOURCE_DIR}/dma.c
    ${SOFTGPU_SOURCE_DIR}/wddm2.c
    platform_jh7110.c)

add_library(jh7110dc MODULE
    ${SOURCE}
    jh7110dc.rc
    ${CMAKE_CURRENT_BINARY_DIR}/jh7110dc.def)

target_compile_definitions(jh7110dc PRIVATE
    REACTOS_WDDM_TARGET_LEVEL=${REACTOS_WDDM_TARGET_LEVEL}
    REACTOS_WDDM_TARGET_INTERFACE_VERSION=${REACTOS_WDDM_TARGET_INTERFACE_VERSION})

set_module_type(jh7110dc kernelmodedriver)
target_link_libraries(jh7110dc displib jh7110disp)
add_importlibs(jh7110dc ntoskrnl hal)
add_pch(jh7110dc ${SOFTGPU_SOURCE_DIR}/softgpu.h "${SOURCE}")
add_cd_file(TARGET jh7110dc DESTINATION reactos/system32/drivers FOR all)
add_driver_inf(jh7110dc jh7110dc.inf)
add_registry_inf(jh7110dc_reg.inf)
