# PROJECT:  ReactOS WDDM Null/Software GPU Miniport
# LICENSE:  GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:  Build file for BasicDisplay.sys — configurable WDDM software miniport
#           with a physical scanout provider and software command engine.
# COPYRIGHT: Copyright 2024 ReactOS WDDM Team
#
# Build order in drivers/directx:
#   1. dxgmms2   — owns the Win11 scheduler/VidMm module boundary
#   2. dxgkrnl   — imports dxgmms2; owns the internal resolver device
#   3. softgpu   — resolves DxgkInitialize through the static displib wrapper

include(${REACTOS_SOURCE_DIR}/sdk/lib/drivers/softgpu/softgpu.cmake)
spec2def(BasicDisplay.sys ${SOFTGPU_ENGINE_SPEC} ADD_IMPORTLIB)

add_library(softgpu MODULE
    platform_root.c
    softgpu.rc
    ${CMAKE_CURRENT_BINARY_DIR}/BasicDisplay.def)

set_target_properties(softgpu PROPERTIES OUTPUT_NAME "BasicDisplay")

set_module_type(softgpu kernelmodedriver)
target_link_libraries(softgpu softgpu_engine displib)
add_importlibs(softgpu ntoskrnl hal)
add_cd_file(TARGET softgpu DESTINATION reactos/system32/drivers FOR all)
add_driver_inf(softgpu basicdisplay.inf)
