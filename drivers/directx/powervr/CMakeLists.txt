#
# PROJECT:     LiberNT PowerVR Rogue WDDM miniport
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     Build the render-only WDDM miniport for Imagination PowerVR Rogue GPUs
# COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
#

remove_definitions(-D_WIN32_WINNT=${REACTOS_TARGET_NT})
remove_definitions(-DWINVER=${REACTOS_TARGET_NT})
add_definitions(-D_WIN32_WINNT=0x0601 -DWINVER=0x0601)
add_definitions(-DNTDDI_VERSION=0x06010000)

remove_definitions(-DDXGKDDI_INTERFACE_VERSION=0x1052)
add_definitions(-DDXGKDDI_INTERFACE_VERSION=0x11007)

list(APPEND SOURCE
    powervr.c
    pvr_k1.c
    pvr_k1_power.c)

list(APPEND PVR_CORE_SOURCE
    glue/pvr_glue.c
    upstream/pvr_ccb.c
    upstream/pvr_cccb.c
    upstream/pvr_context.c
    upstream/pvr_device.c
    upstream/pvr_device_info.c
    upstream/pvr_drv.c
    upstream/pvr_dump.c
    upstream/pvr_free_list.c
    upstream/pvr_fw.c
    upstream/pvr_fw_meta.c
    upstream/pvr_fw_mips.c
    upstream/pvr_fw_riscv.c
    upstream/pvr_fw_startstop.c
    upstream/pvr_fw_trace.c
    upstream/pvr_fw_util.c
    upstream/pvr_gem.c
    upstream/pvr_hwrt.c
    upstream/pvr_job.c
    upstream/pvr_mmu.c
    upstream/pvr_power.c
    upstream/pvr_queue.c
    upstream/pvr_stream.c
    upstream/pvr_stream_defs.c
    upstream/pvr_sync.c
    upstream/pvr_vm.c
    upstream/pvr_vm_mips.c)

add_library(powervr_core ${PVR_CORE_SOURCE})
add_dependencies(powervr_core bugcodes xdk)
target_compile_definitions(powervr_core PRIVATE __KERNEL__)
target_link_libraries(powervr_core PUBLIC drm_core)
target_include_directories(powervr_core PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/glue
    ${CMAKE_CURRENT_SOURCE_DIR}/upstream)
if(NOT MSVC)
    target_compile_options(powervr_core PRIVATE -Wno-unused-function)
endif()

add_library(powervr MODULE ${SOURCE} powervr.rc)
set_module_type(powervr kernelmodedriver)
target_link_libraries(powervr powervr_core drm_core linux_compat displib libcntpr ${PSEH_LIB})
add_importlibs(powervr ntoskrnl hal)
add_cd_file(TARGET powervr DESTINATION reactos/system32/drivers FOR all)
add_driver_inf(powervr powervr.inf)
add_cd_file(FILE ${CMAKE_CURRENT_SOURCE_DIR}/fw/rogue_36.29.52.182_v1.fw DESTINATION reactos/system32/drivers/powervr NO_CAB FOR all)
