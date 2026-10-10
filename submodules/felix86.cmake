# PROJECT:     LiberNT Build System
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     Describe the felix86 feed sources built into the AMD64 emulator
# COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>

set(FELIX86_AVAILABLE OFF)
set(FELIX86_SOURCE_DIR "${REACTOS_SOURCE_DIR}/submodules/felix86")
set(_felix86_external "${FELIX86_SOURCE_DIR}/external")

if(NOT EXISTS "${FELIX86_SOURCE_DIR}/src/felix86/v2/recompiler.cpp")
    message(STATUS "felix86: the felix86 feed is not checked out; skipping it. "
        "Run scripts/feeds update felix86 to build it.")
    return()
endif()

foreach(_felix86_dependency biscuit/src/assembler.cpp zydis/src/Decoder.c
        zydis/dependencies/zycore/src/Zycore.c fmt/include/fmt/format.h
        berkeley-softfloat-3/CMakeLists.txt cephes/src/sinll.c)
    if(NOT EXISTS "${_felix86_external}/${_felix86_dependency}")
        message(FATAL_ERROR "felix86 is enabled but external/${_felix86_dependency} is missing. "
            "Use -DENABLE_FELIX86=OFF to disable it explicitly.")
    endif()
endforeach()

set(_felix86_patch "${CMAKE_CURRENT_LIST_DIR}/felix86-nt.patch")
find_program(FELIX86_GIT_EXECUTABLE git)
if(NOT FELIX86_GIT_EXECUTABLE)
    message(FATAL_ERROR "felix86 is enabled but Git is unavailable to apply ${_felix86_patch}. "
        "Use -DENABLE_FELIX86=OFF to disable it explicitly.")
endif()
execute_process(
    COMMAND "${FELIX86_GIT_EXECUTABLE}" apply --reverse --check "${_felix86_patch}"
    WORKING_DIRECTORY "${FELIX86_SOURCE_DIR}"
    RESULT_VARIABLE _felix86_patch_applied
    OUTPUT_QUIET
    ERROR_QUIET)
if(NOT _felix86_patch_applied EQUAL 0)
    execute_process(
        COMMAND "${FELIX86_GIT_EXECUTABLE}" apply "${_felix86_patch}"
        WORKING_DIRECTORY "${FELIX86_SOURCE_DIR}"
        RESULT_VARIABLE _felix86_patch_result
        OUTPUT_VARIABLE _felix86_patch_output
        ERROR_VARIABLE _felix86_patch_error)
    if(NOT _felix86_patch_result EQUAL 0)
        message(FATAL_ERROR "Failed to apply ${_felix86_patch}: ${_felix86_patch_output}${_felix86_patch_error}")
    endif()
endif()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_felix86_patch}")

execute_process(
    COMMAND "${FELIX86_GIT_EXECUTABLE}" describe --tags --always
    WORKING_DIRECTORY "${FELIX86_SOURCE_DIR}"
    OUTPUT_VARIABLE FELIX86_VERSION
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET)
if(NOT FELIX86_VERSION)
    set(FELIX86_VERSION "unknown")
endif()

set(FELIX86_CORE_SOURCES)
foreach(_felix86_source
        v2/recompiler.cpp
        v2/handlers.cpp
        v2/optimizer.cpp
        common/x87.cpp
        common/xsave.cpp
        common/utility.cpp
        common/feature.cpp
        common/print.cpp
        common/process_lock.cpp
        hle/cpuid.cpp)
    list(APPEND FELIX86_CORE_SOURCES "${FELIX86_SOURCE_DIR}/src/felix86/${_felix86_source}")
endforeach()

file(GLOB FELIX86_BISCUIT_SOURCES CONFIGURE_DEPENDS "${_felix86_external}/biscuit/src/*.cpp")

file(GLOB FELIX86_ZYDIS_SOURCES CONFIGURE_DEPENDS
    "${_felix86_external}/zydis/src/*.c"
    "${_felix86_external}/zydis/dependencies/zycore/src/*.c")
list(FILTER FELIX86_ZYDIS_SOURCES EXCLUDE REGEX "/(Encoder|EncoderData|ArgParse)\\.c$")

file(STRINGS "${_felix86_external}/berkeley-softfloat-3/CMakeLists.txt" _felix86_softfloat_lines
    REGEX "source/[A-Za-z0-9_/]+\\.c")
set(FELIX86_SOFTFLOAT_SOURCES)
foreach(_felix86_line IN LISTS _felix86_softfloat_lines)
    string(REGEX MATCH "source/[A-Za-z0-9_/]+\\.c" _felix86_source "${_felix86_line}")
    list(APPEND FELIX86_SOFTFLOAT_SOURCES "${_felix86_external}/berkeley-softfloat-3/${_felix86_source}")
endforeach()
list(REMOVE_DUPLICATES FELIX86_SOFTFLOAT_SOURCES)

file(GLOB FELIX86_CEPHES_SOURCES CONFIGURE_DEPENDS "${_felix86_external}/cephes/src/*.c")

set(FELIX86_INCLUDE_DIRECTORIES
    "${FELIX86_SOURCE_DIR}/src"
    "${_felix86_external}/biscuit/include"
    "${_felix86_external}/zydis/include"
    "${_felix86_external}/zydis/dependencies/zycore/include"
    "${_felix86_external}/zydis/src"
    "${_felix86_external}/fmt/include"
    "${_felix86_external}/berkeley-softfloat-3/source/include"
    "${_felix86_external}/berkeley-softfloat-3/source/RISCV"
    "${_felix86_external}/cephes/include"
    "${_felix86_external}/FEX/include")

set(FELIX86_SOFTFLOAT_DEFINITIONS
    LITTLEENDIAN=1
    SOFTFLOAT_BUILTIN_CLZ=1
    SOFTFLOAT_INTRINSIC_INT128=1
    SOFTFLOAT_FAST_INT64=1)

set(FELIX86_ZYDIS_DEFINITIONS
    ZYDIS_STATIC_BUILD
    ZYCORE_STATIC_BUILD)

file(RELATIVE_PATH _felix86_relative "${REACTOS_SOURCE_DIR}" "${FELIX86_SOURCE_DIR}")
string(LENGTH "/${_felix86_relative}/" FELIX86_SOURCE_PATH_SIZE)

set(FELIX86_AVAILABLE ON)
