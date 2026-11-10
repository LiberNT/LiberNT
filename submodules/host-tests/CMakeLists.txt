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
option(HOST_TEST_I8042 "Build native PS/2 controller tests" ON)
option(HOST_TEST_NDIS "Build native NDIS WDI tests" ON)

if(BUILD_TESTING AND HOST_TEST_NDIS)
    add_subdirectory(ndis)
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
