# PROJECT:     ReactOS host-native tests
# FILE:        submodules/host-tests/CMakeLists.txt
# PURPOSE:     Host-native test suite build configuration
#
# SPDX-FileCopyrightText: 2026 Ahmed ARIF
# SPDX-License-Identifier: GPL-3.0-only

cmake_minimum_required(VERSION 3.16)
project(reactos_host_tests LANGUAGES C)
include(CTest)

get_filename_component(REACTOS_HOST_SOURCE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
option(HOST_TEST_MMCC "Build native memory-manager and cache-manager tests" ON)
option(HOST_TEST_KE "Build native kernel executive tests" ON)
option(HOST_TEST_NTDLL "Build native NT loader and runtime tests" ON)
option(HOST_TEST_RTL "Build native kernel runtime library tests" ON)
option(HOST_TEST_FSRTL "Build native file system runtime library tests" ON)
option(HOST_TEST_LNTEXPAND "Build native storage expander tests" ON)
option(HOST_TEST_MKDISKIMG "Build native disk image creator tests" ON)
option(HOST_TEST_FATTEN "Build native FAT image creator tests" ON)
option(HOST_TEST_NTFSWIM "Build native WIM backed NTFS volume tests" ON)
option(HOST_TEST_RDSCRATCH "Build native boot RAM volume write space tests" ON)
option(HOST_TEST_I8042 "Build native PS/2 controller tests" ON)
option(HOST_TEST_ACPI "Build native firmware table tests" ON)
option(HOST_TEST_HALTIME "Build native HAL time arithmetic tests" ON)
option(HOST_TEST_NDIS "Build native NDIS WDI tests" ON)
option(HOST_TEST_DHCPCSVC "Build native DHCP client tests" ON)
option(HOST_TEST_EXT4 "Build native ext4 driver core tests" ON)
option(HOST_TEST_USB "Build native USB host controller tests" ON)
option(HOST_TEST_STORPORT "Build native Storport tests" ON)
option(HOST_TEST_DXGKRNL "Build native DirectX graphics kernel tests" ON)
option(HOST_TEST_BOOTLOADER "Build native boot loader tests" ON)
option(HOST_TEST_APPLEAGX "Build native Apple graphics firmware data tests" ON)

if(BUILD_TESTING AND HOST_TEST_DHCPCSVC)
    add_subdirectory(dhcpcsvc)
endif()

if(BUILD_TESTING AND HOST_TEST_EXT4)
    add_subdirectory(ext4)
endif()

if(BUILD_TESTING AND HOST_TEST_USB)
    add_subdirectory(usb)
endif()

if(BUILD_TESTING AND HOST_TEST_STORPORT)
    add_subdirectory(storport)
endif()

if(BUILD_TESTING AND HOST_TEST_DXGKRNL)
    add_subdirectory(dxgkrnl)
endif()

if(BUILD_TESTING AND HOST_TEST_BOOTLOADER)
    add_subdirectory(bootloader)
endif()

if(BUILD_TESTING AND HOST_TEST_APPLEAGX)
    add_subdirectory(appleagx)
endif()

if(BUILD_TESTING AND HOST_TEST_NDIS)
    add_subdirectory(ndis)
endif()

if(BUILD_TESTING AND HOST_TEST_ACPI)
    add_subdirectory(acpival)
    add_subdirectory(acpirouting)
endif()

if(BUILD_TESTING AND HOST_TEST_HALTIME)
    add_subdirectory(haltime)
endif()

if(BUILD_TESTING AND HOST_TEST_I8042)
    add_subdirectory(i8042)
endif()

if(BUILD_TESTING AND HOST_TEST_KE)
    add_subdirectory(ke)
endif()

if(BUILD_TESTING AND HOST_TEST_MMCC)
    add_subdirectory(mmcc)
endif()

if(BUILD_TESTING AND HOST_TEST_NTDLL)
    add_subdirectory(ntdll)
endif()

if(BUILD_TESTING AND HOST_TEST_RTL)
    add_subdirectory(rtl)
endif()

if(BUILD_TESTING AND HOST_TEST_FSRTL)
    add_subdirectory(fsrtl)
endif()

if(BUILD_TESTING AND HOST_TEST_LNTEXPAND)
    add_subdirectory(lntexpand)
endif()

if(BUILD_TESTING AND HOST_TEST_MKDISKIMG)
    add_subdirectory(mkdiskimg)
endif()

if(BUILD_TESTING AND HOST_TEST_FATTEN)
    add_subdirectory(fatten)
endif()

if(BUILD_TESTING AND HOST_TEST_NTFSWIM AND NOT CMAKE_HOST_WIN32)
    add_subdirectory(ntfswim)
endif()

if(BUILD_TESTING AND HOST_TEST_RDSCRATCH AND NOT CMAKE_HOST_WIN32)
    add_subdirectory(rdscratch)
endif()
