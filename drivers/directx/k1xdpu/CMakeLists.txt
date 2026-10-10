#
# PROJECT:     LiberNT SpacemiT K1 display miniport
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     Build the K1 DPU/HDMI platform provider over the software GPU engine
# COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
#

include(${REACTOS_SOURCE_DIR}/sdk/lib/drivers/softgpu/softgpu.cmake)
spec2def(k1xdpu.sys ${SOFTGPU_ENGINE_SPEC} ADD_IMPORTLIB)

remove_definitions(-D_WIN32_WINNT=${REACTOS_TARGET_NT})
remove_definitions(-DWINVER=${REACTOS_TARGET_NT})
add_definitions(-D_WIN32_WINNT=0x0601 -DWINVER=0x0601)
add_definitions(-DNTDDI_VERSION=0x06010000)

remove_definitions(-DDXGKDDI_INTERFACE_VERSION=0x1052)
add_definitions(-DDXGKDDI_INTERFACE_VERSION=0x11007)

include_directories(${REACTOS_SOURCE_DIR}/drivers/directx/powervr)

add_softgpu_engine(k1xdpu_softgpu DEFINITIONS
    SOFTGPU_PLATFORM_HARDWARE_3D=1
    SOFTGPU_PLATFORM_PRIVATE_ESCAPE=1
    SOFTGPU_PLATFORM_DEVICE_CONTEXT=1)

list(APPEND SOURCE
    k1x_gpu.c
    k1x_plane.c
    platform_k1x.c)

add_library(k1xdpu MODULE
    ${SOURCE}
    ${REACTOS_SOURCE_DIR}/drivers/directx/powervr/pvr_k1_power.c
    k1xdpu.rc
    ${CMAKE_CURRENT_BINARY_DIR}/k1xdpu.def)

set_module_type(k1xdpu kernelmodedriver)
target_link_libraries(k1xdpu k1xdpu_softgpu displib powervr_core drm_core linux_compat libcntpr ${PSEH_LIB})
add_importlibs(k1xdpu ntoskrnl hal)
add_pch(k1xdpu ${SOFTGPU_ENGINE_DIR}/softgpu.h "${SOURCE}")
add_cd_file(TARGET k1xdpu DESTINATION reactos/system32/drivers FOR all)
add_driver_inf(k1xdpu k1xdpu.inf)
add_registry_inf(k1xdpu_reg.inf)
