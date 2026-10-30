remove_definitions(-D_WIN32_WINNT=${REACTOS_TARGET_NT})
remove_definitions(-DWINVER=${REACTOS_TARGET_NT})
add_definitions(-D_WIN32_WINNT=0x0603 -DWINVER=0x0603)
add_definitions(-DNTDDI_VERSION=0x06030000)

remove_definitions(-DDXGKDDI_INTERFACE_VERSION=0x1052)
add_definitions(-DDXGKDDI_INTERFACE_VERSION=0x11007)

include_directories(${REACTOS_SOURCE_DIR}/sdk/lib/drivers/softgpu)

add_library(hdgfx MODULE
    core/hdgfx_blt.c
    core/hdgfx_device.c
    core/hdgfx_edid.c
    core/hdgfx_gmbus.c
    core/hdgfx_ilk.c
    core/hdgfx_ilk_modeset.c
    core/hdgfx_ivb_parser.c
    core/hdgfx_ivb_ring.c
    core/hdgfx_opregion.c
    core/hdgfx_ring.c
    core/hdgfx_vbt.c
    hdgfx_alloc.c
    hdgfx_display.c
    hdgfx_dma.c
    hdgfx_platform.c
    hdgfx_wddm.c
    hdgfx.rc)

set_module_type(hdgfx kernelmodedriver ENTRYPOINT HdgfxDriverEntry 8)
target_link_libraries(hdgfx displib ${PSEH_LIB})
add_importlibs(hdgfx ntoskrnl hal)
add_cd_file(TARGET hdgfx DESTINATION reactos/system32/drivers FOR all)
add_driver_inf(hdgfx hdgfx.inf)
