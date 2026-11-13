
if(REACTOS_BOOTSTRAP_BOOTLOADER)
    add_subdirectory(rtl)
    return()
endif()

if(ARCH STREQUAL "i386" OR ARCH STREQUAL "amd64")
    set(FREELDR_HAS_BIOS_BOOT TRUE)
else()
    set(FREELDR_HAS_BIOS_BOOT FALSE)
endif()

set(FREELDR_HAS_OFW_BOOT FALSE)
if(ARCH STREQUAL "ppc")
    set(FREELDR_HAS_OFW_BOOT TRUE)
endif()

option(FREELDR_WIM_RAMDISK "Enable FreeLDR boot.wim ramdisk decompression support" OFF)

include(${REACTOS_SOURCE_DIR}/sdk/cmake/boot_images.cmake)

add_subdirectory(bootdata)
if(NOT REACTOS_BOOTSTRAP_BOOTLOADER)
    add_subdirectory(freeldr)
endif()
add_subdirectory(rtl)

add_cd_file(FILE ${CMAKE_CURRENT_SOURCE_DIR}/remaster.cmd DESTINATION loader NO_CAB FOR bootcd)
add_cd_file(FILE ${CMAKE_CURRENT_SOURCE_DIR}/remaster.sh DESTINATION loader NO_CAB FOR bootcd)
