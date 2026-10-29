include(${REACTOS_SOURCE_DIR}/sdk/lib/drivers/softgpu/softgpu.cmake)
spec2def(hdgfx.sys ${SOFTGPU_ENGINE_SPEC} ADD_IMPORTLIB)

remove_definitions(-D_WIN32_WINNT=${REACTOS_TARGET_NT})
remove_definitions(-DWINVER=${REACTOS_TARGET_NT})
add_definitions(-D_WIN32_WINNT=0x0603 -DWINVER=0x0603)
add_definitions(-DNTDDI_VERSION=0x06030000)

remove_definitions(-DDXGKDDI_INTERFACE_VERSION=0x1052)
add_definitions(-DDXGKDDI_INTERFACE_VERSION=0x11007)

add_softgpu_engine(hdgfx_softgpu DEFINITIONS
    SOFTGPU_PLATFORM_ADAPTER_SERVICES=1
    SOFTGPU_PLATFORM_ACPI_EVENTS=1
    SOFTGPU_PLATFORM_MONITOR_POWER=1
    SOFTGPU_PLATFORM_INTERRUPTS=1
    SOFTGPU_PLATFORM_HARDWARE_2D=1
    SOFTGPU_PLATFORM_PHYSICAL_ADDRESSING=1)

add_library(hdgfx MODULE
    core/hdgfx_blt.c
    core/hdgfx_device.c
    core/hdgfx_edid.c
    core/hdgfx_gmbus.c
    core/hdgfx_ilk.c
    core/hdgfx_ilk_modeset.c
    core/hdgfx_opregion.c
    core/hdgfx_ring.c
    core/hdgfx_vbt.c
    hdgfx_platform.c
    hdgfx_wddm.c
    hdgfx.rc
    ${CMAKE_CURRENT_BINARY_DIR}/hdgfx.def)

set_module_type(hdgfx kernelmodedriver ENTRYPOINT HdgfxDriverEntry 8)
target_link_libraries(hdgfx hdgfx_softgpu displib ${PSEH_LIB})
add_importlibs(hdgfx ntoskrnl hal)
add_cd_file(TARGET hdgfx DESTINATION reactos/system32/drivers FOR all)
add_driver_inf(hdgfx hdgfx.inf)
