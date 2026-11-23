
# TODO: BIOS boot moves into bootloader/; FreeLoader is no longer built
set(FREELDR_HAS_BIOS_BOOT FALSE)

set(BOOTLOADER_OFW FALSE)
if(ARCH STREQUAL "ppc")
    set(BOOTLOADER_OFW TRUE)
endif()

option(FREELDR_WIM_RAMDISK "Enable FreeLDR boot.wim ramdisk decompression support" OFF)

include(${REACTOS_SOURCE_DIR}/sdk/cmake/boot_images.cmake)

add_subdirectory(bootdata)
#if(NOT BOOTLOADER_OFW)
#    add_subdirectory(freeldr)
#endif()
add_subdirectory(rtl)

add_cd_file(FILE ${CMAKE_CURRENT_SOURCE_DIR}/remaster.cmd DESTINATION loader NO_CAB FOR bootcd)
add_cd_file(FILE ${CMAKE_CURRENT_SOURCE_DIR}/remaster.sh DESTINATION loader NO_CAB FOR bootcd)
