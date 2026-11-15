# PROJECT:     LiberNT host-native tests
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     ext4 driver core on real mkfs.ext4 images, checked by e2fsck: file operations, growing and shrinking
# COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>

set(LWEXT4_DIR ${REACTOS_HOST_SOURCE_ROOT}/sdk/lib/3rdparty/lwext4)
set(EXT4_CORE_DIR ${REACTOS_HOST_SOURCE_ROOT}/drivers/filesystems/ext4/core)

file(GLOB LWEXT4_SOURCE ${LWEXT4_DIR}/src/*.c)
list(REMOVE_ITEM LWEXT4_SOURCE ${LWEXT4_DIR}/src/ext4.c ${LWEXT4_DIR}/src/ext4_mbr.c)

add_executable(ext4_core t_e4core.c ${EXT4_CORE_DIR}/e4core.c ${EXT4_CORE_DIR}/e4resize.c ${EXT4_CORE_DIR}/e4stage.c ${LWEXT4_SOURCE})
target_include_directories(ext4_core PRIVATE ${LWEXT4_DIR}/include ${EXT4_CORE_DIR})
target_compile_definitions(ext4_core PRIVATE
    CONFIG_USE_DEFAULT_CFG=1 CONFIG_HAVE_PORT_DEBUG=1 CONFIG_HAVE_OWN_ERRNO=1 CONFIG_UNALIGNED_ACCESS=1)
target_compile_options(ext4_core PRIVATE -w)

# One run per on-disk layout the driver mounts; each needs mkfs.ext4 and e2fsck on the host and is skipped without them
foreach(LAYOUT default nojournal nocsum ext3 ext2 bs1k)
    add_test(NAME ext4.core.${LAYOUT}
             COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/stress.sh $<TARGET_FILE:ext4_core> ${LAYOUT})
    set_tests_properties(ext4.core.${LAYOUT} PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 600)
endforeach()

# A power cut at many points of the same run: recovery must leave a volume e2fsck accepts
foreach(LAYOUT default bs1k smalljournal)
    add_test(NAME ext4.powercut.${LAYOUT}
             COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/powercut.sh $<TARGET_FILE:ext4_core> ${LAYOUT})
    set_tests_properties(ext4.powercut.${LAYOUT} PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 900)
endforeach()
