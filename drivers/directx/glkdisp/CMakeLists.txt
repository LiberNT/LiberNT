include(${REACTOS_SOURCE_DIR}/sdk/lib/drivers/softgpu/softgpu.cmake)
spec2def(glkdisp.sys ${SOFTGPU_ENGINE_SPEC} ADD_IMPORTLIB)

remove_definitions(-D_WIN32_WINNT=${REACTOS_TARGET_NT})
remove_definitions(-DWINVER=${REACTOS_TARGET_NT})
add_definitions(-D_WIN32_WINNT=0x0603 -DWINVER=0x0603)
add_definitions(-DNTDDI_VERSION=0x06030000)

remove_definitions(-DDXGKDDI_INTERFACE_VERSION=0x1052)
add_definitions(-DDXGKDDI_INTERFACE_VERSION=0x11007)

add_softgpu_engine(glkdisp_softgpu DEFINITIONS
    SOFTGPU_PLATFORM_ADAPTER_SERVICES=1)

add_library(glkdisp MODULE
    ${REACTOS_SOURCE_DIR}/drivers/directx/softgpu/platform_root.c
    glk_backlight.c
    glkdisp.rc
    ${CMAKE_CURRENT_BINARY_DIR}/glkdisp.def)

set_module_type(glkdisp kernelmodedriver)
target_link_libraries(glkdisp glkdisp_softgpu displib)
add_importlibs(glkdisp ntoskrnl hal)
add_cd_file(TARGET glkdisp DESTINATION reactos/system32/drivers FOR all)
add_driver_inf(glkdisp glkdisp.inf)
