#
# PROJECT:     LiberNT Apple Display Coprocessor Driver
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     Build the Apple silicon display coprocessor platform provider over the software GPU engine
# COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
#

include(${REACTOS_SOURCE_DIR}/sdk/lib/drivers/softgpu/softgpu.cmake)
spec2def(appledcp.sys ${SOFTGPU_ENGINE_SPEC} ADD_IMPORTLIB)

add_softgpu_engine(appledcp_softgpu DEFINITIONS
    SOFTGPU_PLATFORM_DRIVER_ENTRY=1)

add_library(appledcp MODULE
    dcp.c
    platform_apple.c
    appledcp.rc
    ${CMAKE_CURRENT_BINARY_DIR}/appledcp.def)

set_module_type(appledcp kernelmodedriver)
target_link_libraries(appledcp appledcp_softgpu displib applertkit fdtdrv)
add_importlibs(appledcp ntoskrnl hal)
add_cd_file(TARGET appledcp DESTINATION reactos/system32/drivers FOR all)
add_driver_inf(appledcp appledcp.inf)
add_registry_inf(appledcp_reg.inf)
