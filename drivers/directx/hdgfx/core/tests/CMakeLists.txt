# SPDX-License-Identifier: GPL-3.0-or-later
# Standalone host tests: cmake -S drivers/directx/hdgfx/core/tests -B <build> && ctest --test-dir <build>
cmake_minimum_required(VERSION 3.16)
project(hdgfx_core_tests C)
enable_testing()

set(HDGFX_X201_OPREGION "" CACHE FILEPATH "X201 OpRegion dump (i915_opregion) for the VBT test")

add_executable(hdgfx_x201_test x201_test.c ../hdgfx_blt.c ../hdgfx_device.c ../hdgfx_edid.c ../hdgfx_gmbus.c ../hdgfx_ilk.c ../hdgfx_ilk_modeset.c ../hdgfx_ivb_ring.c ../hdgfx_opregion.c ../hdgfx_ring.c ../hdgfx_vbt.c)
target_include_directories(hdgfx_x201_test PRIVATE ..)
set_target_properties(hdgfx_x201_test PROPERTIES C_STANDARD 99 C_STANDARD_REQUIRED YES)
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(hdgfx_x201_test PRIVATE -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(hdgfx_x201_test PRIVATE -fsanitize=address,undefined)
endif()

add_executable(hdgfx_ivb_test ivb_test.c ../hdgfx_blt.c ../hdgfx_device.c ../hdgfx_edid.c ../hdgfx_gmbus.c ../hdgfx_ilk.c ../hdgfx_ilk_modeset.c ../hdgfx_ivb_parser.c ../hdgfx_ivb_ring.c ../hdgfx_ring.c)
target_include_directories(hdgfx_ivb_test PRIVATE ..)
set_target_properties(hdgfx_ivb_test PROPERTIES C_STANDARD 99 C_STANDARD_REQUIRED YES)
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(hdgfx_ivb_test PRIVATE -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(hdgfx_ivb_test PRIVATE -fsanitize=address,undefined)
endif()

add_test(NAME hdgfx_ivb COMMAND hdgfx_ivb_test ${CMAKE_CURRENT_SOURCE_DIR}/fixtures)
set_tests_properties(hdgfx_ivb PROPERTIES TIMEOUT 30)

add_test(NAME hdgfx_x201 COMMAND hdgfx_x201_test ${CMAKE_CURRENT_SOURCE_DIR}/fixtures ${HDGFX_X201_OPREGION})
set_tests_properties(hdgfx_x201 PROPERTIES TIMEOUT 30)
