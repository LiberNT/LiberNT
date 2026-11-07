list(APPEND SOURCE
    core.c
    pci.c
    power.c
    queue.c
    scsi.c)

if(ARCH STREQUAL "arm64")
    list(APPEND SOURCE apple.c)
else()
    list(APPEND SOURCE platform_none.c)
endif()

add_library(stornvme MODULE ${SOURCE} stornvme.rc)

set_module_type(stornvme kernelmodedriver)
add_importlibs(stornvme storport ntoskrnl hal)
if(ARCH STREQUAL "arm64")
    target_link_libraries(stornvme applertkit fdtdrv)
endif()

add_cd_file(TARGET stornvme DESTINATION reactos/system32/drivers NO_CAB FOR all)

add_driver_inf(stornvme stornvme.inf)
