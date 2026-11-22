
include(${REACTOS_SOURCE_DIR}/sdk/lib/drivers/softgpu/softgpu.cmake)
spec2def(qemuvga.sys ${SOFTGPU_ENGINE_SPEC} ADD_IMPORTLIB)

remove_definitions(-D_WIN32_WINNT=${REACTOS_TARGET_NT})
remove_definitions(-DWINVER=${REACTOS_TARGET_NT})
add_definitions(-D_WIN32_WINNT=0x0603 -DWINVER=0x0603)
add_definitions(-DNTDDI_VERSION=0x06030000)

remove_definitions(-DDXGKDDI_INTERFACE_VERSION=0x1052)
add_definitions(-DDXGKDDI_INTERFACE_VERSION=0x11007)

add_library(qemuvga MODULE
    platform_qemuvga.c
    qemuvga.rc
    ${CMAKE_CURRENT_BINARY_DIR}/qemuvga.def)

set_module_type(qemuvga kernelmodedriver)
target_link_libraries(qemuvga softgpu_engine displib)
add_importlibs(qemuvga ntoskrnl hal)
add_cd_file(TARGET qemuvga DESTINATION reactos/system32/drivers FOR all)
add_driver_inf(qemuvga qemuvga.inf)
