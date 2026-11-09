# SPDX-License-Identifier: GPL-3.0-only
# SPDX-FileCopyrightText: 2026 Ahmed ARIF
#
# Full ICU icu.dll of a LiberNT release. The image keeps the stub icu.dll; the
# second stage installs libernt-icu-<release>-<arch>.msi over it. The build
# only catalogs the payload icu.dll so code integrity accepts the installed one.
# sdk/tools/icu_payload.py builds the payload.

set(ICU_PAYLOAD_RELEASE "72.1-1")
set(ICU_PAYLOAD_SHA256_arm64 "9d5d74e3f85066116953d168303d0577e2a218bfc48ed12b003fb9339906e8c8")
set(ICU_PAYLOAD "" CACHE FILEPATH "Local ICU payload .zip used instead of downloading the release asset")

include("${CMAKE_CURRENT_LIST_DIR}/payload-download.cmake")

if(ARM64EC_RUNTIME)
    return()
endif()

set(_payload_name "libernt-icu-${ICU_PAYLOAD_RELEASE}-${ARCH}.zip")
set(_payload_dir "${REACTOS_BINARY_DIR}/submodules/icu-payload")
set(_payload_extract "${_payload_dir}/${ICU_PAYLOAD_RELEASE}")
file(MAKE_DIRECTORY "${_payload_dir}")

if(ICU_PAYLOAD)
    get_filename_component(_payload_zip "${ICU_PAYLOAD}" ABSOLUTE)
    if(NOT EXISTS "${_payload_zip}")
        message(FATAL_ERROR "ICU_PAYLOAD does not exist: ${_payload_zip}")
    endif()
else()
    set(_payload_hash "${ICU_PAYLOAD_SHA256_${ARCH}}")
    if(NOT _payload_hash)
        return()
    endif()
    set(_payload_zip "${_payload_dir}/${_payload_name}")
    libernt_download_payload(LiberNT/LiberNT "icu-${ICU_PAYLOAD_RELEASE}" "${_payload_name}"
        "${_payload_zip}" "${_payload_hash}" _payload_error)
    if(NOT _payload_error STREQUAL "")
        message(STATUS "ICU: downloading the payload failed (${_payload_error}); "
            "the second-stage icu.dll will not be cataloged.")
        return()
    endif()
endif()

file(SHA256 "${_payload_zip}" _payload_zip_hash)
set(_payload_stamp "${_payload_extract}.sha256")
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

if(NOT EXISTS "${_payload_extract}/system32/icu.dll")
    message(FATAL_ERROR "The ICU payload lacks system32/icu.dll")
endif()
set_property(GLOBAL APPEND PROPERTY CI_SYSTEM_FILES "${_payload_extract}/system32/icu.dll")
