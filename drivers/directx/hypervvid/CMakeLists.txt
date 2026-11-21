
include(${REACTOS_SOURCE_DIR}/sdk/lib/drivers/softgpu/softgpu.cmake)
spec2def(hypervvid.sys ${SOFTGPU_ENGINE_SPEC} ADD_IMPORTLIB)

include_directories(${REACTOS_SOURCE_DIR}/drivers/bus/vmbus)

add_library(hypervvid MODULE
    platform_hyperv.c
    hypervvid.rc
    ${CMAKE_CURRENT_BINARY_DIR}/hypervvid.def)

set_module_type(hypervvid kernelmodedriver)
target_link_libraries(hypervvid softgpu_engine displib)
add_importlibs(hypervvid vmbus ntoskrnl hal)
add_cd_file(TARGET hypervvid DESTINATION reactos/system32/drivers FOR all)
add_driver_inf(hypervvid hypervvid.inf)
