
set(BOOTLOADER_OFW FALSE)
if(ARCH STREQUAL "ppc")
    set(BOOTLOADER_OFW TRUE)
endif()

option(BOOTLOADER_WIM_RAMDISK "Boot the live media from a boot.wim RAM disk" OFF)

include(${REACTOS_SOURCE_DIR}/sdk/cmake/boot_images.cmake)

add_subdirectory(bootdata)
