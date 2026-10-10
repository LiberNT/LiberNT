# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Ahmed ARIF <arif193@gmail.com>

if(ARCH STREQUAL "riscv64")
    option(SPACEMIT_K1_SUPPORT "Build SpacemiT K1 SD card images" OFF)

    if(SPACEMIT_K1_SUPPORT)
        set(SPACEMIT_K1_BOOT_DIR "${CMAKE_CURRENT_LIST_DIR}/spacemit-k1")

        set(SPACEMIT_K1_BOOTINFO "${SPACEMIT_K1_BOOT_DIR}/bootinfo_sd.bin")
        set(SPACEMIT_K1_FSBL "${SPACEMIT_K1_BOOT_DIR}/FSBL.bin")
        set(SPACEMIT_K1_OPENSBI "${SPACEMIT_K1_BOOT_DIR}/fw_dynamic.itb")
        set(SPACEMIT_K1_UBOOT "${SPACEMIT_K1_BOOT_DIR}/u-boot.itb")
        set(SPACEMIT_K1_UBOOT_ENV "${SPACEMIT_K1_BOOT_DIR}/env_k1-x.txt")
        foreach(_k1_file BOOTINFO FSBL OPENSBI UBOOT UBOOT_ENV)
            if(NOT EXISTS "${SPACEMIT_K1_${_k1_file}}")
                message(FATAL_ERROR "SpacemiT K1: missing boot file ${SPACEMIT_K1_${_k1_file}}")
            endif()
            set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${SPACEMIT_K1_${_k1_file}}")
        endforeach()

        set(SPACEMIT_K1_FSBL_SECTOR 256)
        set(SPACEMIT_K1_FSBL_BACKUP_SECTOR 1024)
        set(SPACEMIT_K1_OPENSBI_START 2048)
        set(SPACEMIT_K1_OPENSBI_SECTORS 2048)
        set(SPACEMIT_K1_UBOOT_START 4096)
        set(SPACEMIT_K1_UBOOT_SECTORS 8192)
        set(SPACEMIT_K1_BOOT_PARTITION_START_MB 8)

        message(STATUS "RISC-V64: SpacemiT K1 SD card layout enabled")
    endif()

    option(STARFIVE_JH7110_SUPPORT "Build the StarFive JH7110 SD card that boots FreeLoader from USB" OFF)

    if(STARFIVE_JH7110_SUPPORT)
        set(STARFIVE_JH7110_BOOT_DIR "${CMAKE_CURRENT_LIST_DIR}/starfive-jh7110")

        set(STARFIVE_JH7110_SPL "${STARFIVE_JH7110_BOOT_DIR}/u-boot-spl.bin.normal.out")
        set(STARFIVE_JH7110_UBOOT "${STARFIVE_JH7110_BOOT_DIR}/u-boot.itb")
        set(STARFIVE_JH7110_BOOT_SCRIPT "${STARFIVE_JH7110_BOOT_DIR}/boot.scr")
        set(STARFIVE_JH7110_BOOT_SCRIPT_SOURCE "${STARFIVE_JH7110_BOOT_DIR}/boot.cmd")
        set(STARFIVE_JH7110_DTB "${STARFIVE_JH7110_BOOT_DIR}/jh7110-orangepi-rv.dtb")
        foreach(_jh7110_file SPL UBOOT BOOT_SCRIPT BOOT_SCRIPT_SOURCE DTB)
            if(NOT EXISTS "${STARFIVE_JH7110_${_jh7110_file}}")
                message(FATAL_ERROR "StarFive JH7110: missing boot file ${STARFIVE_JH7110_${_jh7110_file}}")
            endif()
            set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${STARFIVE_JH7110_${_jh7110_file}}")
        endforeach()

        set(STARFIVE_JH7110_SPL_GUID "2E54B353-1271-4842-806F-E436D6AF6985")
        set(STARFIVE_JH7110_UBOOT_GUID "BC13C2FF-59E6-4262-A352-B275FD6F7172")
        set(STARFIVE_JH7110_SPL_START 4096)
        set(STARFIVE_JH7110_SPL_SECTORS 4096)
        set(STARFIVE_JH7110_UBOOT_START 8192)
        set(STARFIVE_JH7110_UBOOT_SECTORS 8192)
        set(STARFIVE_JH7110_BOOT_START 16384)
        set(STARFIVE_JH7110_BOOT_SECTORS 131072)

        message(STATUS "RISC-V64: StarFive JH7110 SD card enabled")
    endif()
endif()
