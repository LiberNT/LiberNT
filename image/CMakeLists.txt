
set(BOOTLOADER_OFW FALSE)
if(ARCH STREQUAL "ppc")
    set(BOOTLOADER_OFW TRUE)
endif()

option(BOOTLOADER_WIM_RAMDISK "Boot the live media from a boot.wim RAM disk" OFF)

include(${CMAKE_CURRENT_SOURCE_DIR}/images.cmake)

add_subdirectory(data ${CMAKE_CURRENT_BINARY_DIR}/bootdata)
