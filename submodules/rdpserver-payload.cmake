# PROJECT:     LiberNT Build System
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     Import the prebuilt OpenSSL and FreeRDP libraries of the RDP server payload
# COPYRIGHT:   Copyright 2026 mikuta0407 <mikuta0407@mikuta0407.net>

set(RDPSERVER_PAYLOAD_RELEASE "freerdp-3.32.0-openssl-3.5.4-1")
set(RDPSERVER_PAYLOAD "" CACHE FILEPATH "Local RDP server payload .zip used instead of downloading the release asset")
set(RDPSERVER_PAYLOAD_LIBRARIES libcrypto libssl winpr freerdp freerdp-server)

include("${CMAKE_CURRENT_LIST_DIR}/payload-download.cmake")

set(RDPSERVER_PAYLOAD_ROOT)
set(_payload_name "libernt-rdpserver-${RDPSERVER_PAYLOAD_RELEASE}-${ARCH}-${CMAKE_C_COMPILER_ID}.zip")
set(_payload_dir "${CMAKE_BINARY_DIR}/submodules/rdpserver-payload")
set(_payload_extract "${_payload_dir}/${RDPSERVER_PAYLOAD_RELEASE}")
file(MAKE_DIRECTORY "${_payload_dir}")

if(RDPSERVER_PAYLOAD)
    get_filename_component(_payload_zip "${RDPSERVER_PAYLOAD}" ABSOLUTE)
    if(NOT EXISTS "${_payload_zip}")
        message(FATAL_ERROR "RDPSERVER_PAYLOAD does not exist: ${_payload_zip}")
    endif()
else()
    set(_payload_hash "${RDPSERVER_PAYLOAD_SHA256_${ARCH}_${CMAKE_C_COMPILER_ID}}")
    if(NOT _payload_hash)
        message(STATUS "RDP server: no published payload for ${ARCH} and ${CMAKE_C_COMPILER_ID} in release "
            "${RDPSERVER_PAYLOAD_RELEASE}; building without the server. Set RDPSERVER_PAYLOAD or RDPSERVER_FROM_SOURCE.")
        return()
    endif()
    set(_payload_zip "${_payload_dir}/${_payload_name}")
    libernt_download_payload(LiberNT/LiberNT "rdpserver-${RDPSERVER_PAYLOAD_RELEASE}" "${_payload_name}"
        "${_payload_zip}" "${_payload_hash}" _payload_error)
    if(NOT _payload_error STREQUAL "")
        message(STATUS "RDP server: downloading the payload failed (${_payload_error}); building without the server.")
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

file(READ "${_payload_extract}/payload.json" _payload_json)
string(JSON _payload_arch GET "${_payload_json}" arch)
string(JSON _payload_compiler GET "${_payload_json}" compiler)
if(NOT _payload_arch STREQUAL ARCH OR NOT _payload_compiler STREQUAL CMAKE_C_COMPILER_ID)
    message(FATAL_ERROR "The RDP server payload was built for ${_payload_arch} with ${_payload_compiler}, "
        "not for ${ARCH} with ${CMAKE_C_COMPILER_ID}")
endif()

foreach(_payload_library ${RDPSERVER_PAYLOAD_LIBRARIES})
    set(_payload_file "${_payload_extract}/lib/lib${_payload_library}.a")
    if(NOT EXISTS "${_payload_file}")
        message(FATAL_ERROR "The RDP server payload lacks lib/lib${_payload_library}.a")
    endif()
    add_library(${_payload_library} STATIC IMPORTED)
    set_target_properties(${_payload_library} PROPERTIES
        IMPORTED_LOCATION "${_payload_file}"
        INTERFACE_INCLUDE_DIRECTORIES "${_payload_extract}/include")
endforeach()
set_property(TARGET libssl PROPERTY INTERFACE_LINK_LIBRARIES libcrypto)
set_property(TARGET winpr PROPERTY INTERFACE_LINK_LIBRARIES libssl libcrypto)
set_property(TARGET freerdp PROPERTY INTERFACE_LINK_LIBRARIES winpr)
set_property(TARGET freerdp-server PROPERTY INTERFACE_LINK_LIBRARIES freerdp)
set(RDPSERVER_PAYLOAD_ROOT "${_payload_extract}")
