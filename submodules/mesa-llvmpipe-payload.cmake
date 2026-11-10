# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Ahmed ARIF <arif.ing@outlook.com>
# Prebuilt LLVMpipe OpenGL and Lavapipe Vulkan ICDs from the LiberNT release payload.
# The second stage installs them from the release MSI; the build only catalogs them.

if(NOT ARCH MATCHES "^(amd64|arm64|riscv64)$")
    message(FATAL_ERROR "The LLVMpipe payload exists for amd64, arm64 and riscv64 only.")
endif()
if(ARM64EC_RUNTIME)
    return()
endif()

include("${CMAKE_CURRENT_LIST_DIR}/payload-download.cmake")

set(MESA_LLVMPIPE_PAYLOAD_RELEASE "26.2.2-1")
set(MESA_LLVMPIPE_PAYLOAD_SHA256_amd64 "17f354520ebbe3c67ea62432e9c456964a915e1571f09b250f300f82ace60f4b")
set(MESA_LLVMPIPE_PAYLOAD_SHA256_arm64 "552b71226dc9fbe21ccc4f8eb1e3ff36b3af554e35c09a68af61d3de003f90c0")
set(MESA_LLVMPIPE_PAYLOAD_SHA256_riscv64 "c2a1c78c76e0cc5fb40093b7bf2a84ebd5a05ed0992ea28cd383f692329a418b")
set(MESA_LLVMPIPE_PAYLOAD "" CACHE FILEPATH "Local LLVMpipe payload .zip used instead of downloading the release asset")

set(_payload_name "libernt-mesa-llvmpipe-${MESA_LLVMPIPE_PAYLOAD_RELEASE}-${ARCH}.zip")
set(_payload_dir "${CMAKE_CURRENT_BINARY_DIR}/mesa-llvmpipe-payload")
set(_payload_extract "${_payload_dir}/${MESA_LLVMPIPE_PAYLOAD_RELEASE}")
file(MAKE_DIRECTORY "${_payload_dir}")

if(MESA_LLVMPIPE_PAYLOAD)
    get_filename_component(_payload_zip "${MESA_LLVMPIPE_PAYLOAD}" ABSOLUTE)
    if(NOT EXISTS "${_payload_zip}")
        message(FATAL_ERROR "MESA_LLVMPIPE_PAYLOAD does not exist: ${_payload_zip}")
    endif()
else()
    set(_payload_hash "${MESA_LLVMPIPE_PAYLOAD_SHA256_${ARCH}}")
    if(NOT _payload_hash)
        message(FATAL_ERROR "No published LLVMpipe payload for ${ARCH} release ${MESA_LLVMPIPE_PAYLOAD_RELEASE}; set MESA_LLVMPIPE_PAYLOAD or MESA_LLVMPIPE_FROM_SOURCE.")
    endif()
    set(_payload_zip "${_payload_dir}/${_payload_name}")
    libernt_download_payload(LiberNT/LiberNT "mesa-llvmpipe-${MESA_LLVMPIPE_PAYLOAD_RELEASE}" "${_payload_name}"
        "${_payload_zip}" "${_payload_hash}" _payload_error)
    if(NOT _payload_error STREQUAL "")
        message(FATAL_ERROR "Downloading the LLVMpipe payload failed: ${_payload_error}")
    endif()
endif()

file(SHA256 "${_payload_zip}" _payload_zip_hash)
set(_payload_stamp "${_payload_extract}/.payload-sha256")
set(_payload_extracted_hash "")
if(EXISTS "${_payload_stamp}")
    file(READ "${_payload_stamp}" _payload_extracted_hash)
endif()
if(NOT _payload_extracted_hash STREQUAL _payload_zip_hash)
    file(REMOVE_RECURSE "${_payload_extract}")
    file(ARCHIVE_EXTRACT INPUT "${_payload_zip}" DESTINATION "${_payload_extract}")
    file(WRITE "${_payload_stamp}" "${_payload_zip_hash}")
endif()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_payload_zip}")

foreach(_payload_file mesa_llvmpipe.dll vulkan_lvp.dll lvp_icd.json)
    if(NOT EXISTS "${_payload_extract}/system32/${_payload_file}")
        message(FATAL_ERROR "The LLVMpipe payload lacks system32/${_payload_file}")
    endif()
endforeach()
set_property(GLOBAL APPEND PROPERTY CI_SYSTEM_FILES
    "${_payload_extract}/system32/mesa_llvmpipe.dll" "${_payload_extract}/system32/vulkan_lvp.dll")
message(STATUS "LLVMpipe/Lavapipe payload ${MESA_LLVMPIPE_PAYLOAD_RELEASE} for ${ARCH} from ${_payload_zip}")
