# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Ahmed ARIF <arif.ing@outlook.com>
# Headers of the prebuilt LGPL FFmpeg payload. The decoders bind the libraries at
# run time; the second stage installs them, so the image does not carry them.

set(FFMPEG_PAYLOAD_RELEASE "n8.1.3-1")
set(FFMPEG_PAYLOAD_SHA256_arm64 "c8d237f30aa51387173baf6ff20380c361f9b338b9ce8926887dd08afc8f6e62")
set(FFMPEG_PAYLOAD "" CACHE FILEPATH "Local FFmpeg payload .zip used instead of downloading the release asset")

include("${CMAKE_CURRENT_LIST_DIR}/payload-download.cmake")

if(ARM64EC_RUNTIME)
    return()
endif()

set(_payload_name "libernt-ffmpeg-${FFMPEG_PAYLOAD_RELEASE}-${ARCH}.zip")
set(_payload_dir "${CMAKE_CURRENT_BINARY_DIR}/ffmpeg-payload")
set(_payload_extract "${_payload_dir}/${FFMPEG_PAYLOAD_RELEASE}")
file(MAKE_DIRECTORY "${_payload_dir}")

if(FFMPEG_PAYLOAD)
    get_filename_component(_payload_zip "${FFMPEG_PAYLOAD}" ABSOLUTE)
    if(NOT EXISTS "${_payload_zip}")
        message(FATAL_ERROR "FFMPEG_PAYLOAD does not exist: ${_payload_zip}")
    endif()
else()
    set(_payload_hash "${FFMPEG_PAYLOAD_SHA256_${ARCH}}")
    if(NOT _payload_hash)
        message(STATUS "FFmpeg: no published payload for ${ARCH} release ${FFMPEG_PAYLOAD_RELEASE}; "
            "building the decoders without FFmpeg. Set FFMPEG_PAYLOAD or FFMPEG_FROM_SOURCE.")
        return()
    endif()
    set(_payload_zip "${_payload_dir}/${_payload_name}")
    libernt_download_payload(LiberNT/LiberNT "ffmpeg-${FFMPEG_PAYLOAD_RELEASE}" "${_payload_name}"
        "${_payload_zip}" "${_payload_hash}" _payload_error)
    if(NOT _payload_error STREQUAL "")
        message(STATUS "FFmpeg: downloading the payload failed (${_payload_error}); "
            "building the decoders without FFmpeg.")
        return()
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

if(NOT EXISTS "${_payload_extract}/include/libavcodec/avcodec.h")
    message(FATAL_ERROR "The FFmpeg payload lacks include/libavcodec/avcodec.h")
endif()
file(GLOB _payload_dlls "${_payload_extract}/system32/*.dll")
set_property(GLOBAL APPEND PROPERTY CI_SYSTEM_FILES ${_payload_dlls})
set(FFMPEG_ROOT "${_payload_extract}")
add_custom_target(ffmpeg)
