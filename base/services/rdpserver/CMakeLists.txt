# PROJECT:     LiberNT RDP console server
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     Stage a MinGW build of the RDP console server into the images
# COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif193@gmail.com>

if(NOT ARCH STREQUAL "amd64")
    message(FATAL_ERROR "RDPSERVER_DIR holds an x86_64 build; it can only be staged into amd64 images.")
endif()

set(_rdpserver_bin "${RDPSERVER_DIR}/bin")
set(_rdpserver_files
    "${_rdpserver_bin}/rdpserver.exe"
    "${_rdpserver_bin}/libfreerdp3.dll"
    "${_rdpserver_bin}/libfreerdp-server3.dll"
    "${_rdpserver_bin}/libwinpr3.dll"
    "${_rdpserver_bin}/libssl-3-x64.dll"
    "${_rdpserver_bin}/libcrypto-3-x64.dll"
    "${_rdpserver_bin}/libgcc_s_seh-1.dll"
    "${_rdpserver_bin}/libwinpthread-1.dll")
set(_rdpserver_modules "${_rdpserver_bin}/ossl-modules/legacy.dll")
set(_rdpserver_licenses
    "${RDPSERVER_DIR}/licenses/FreeRDP-LICENSE"
    "${RDPSERVER_DIR}/licenses/OpenSSL-LICENSE")
foreach(_file IN LISTS _rdpserver_files _rdpserver_modules _rdpserver_licenses)
    if(NOT EXISTS "${_file}")
        message(FATAL_ERROR "RDPSERVER_DIR is missing ${_file}")
    endif()
endforeach()

add_cd_file(FILE ${_rdpserver_files} DESTINATION reactos/system32/rdpserver FOR livecd preinstall)
add_cd_file(FILE ${_rdpserver_modules} DESTINATION reactos/system32/rdpserver/ossl-modules FOR livecd preinstall)
add_cd_file(FILE ${_rdpserver_licenses} DESTINATION reactos/system32/rdpserver/licenses FOR livecd preinstall)

# The in-tree configuration keeps the server disabled. Credentials and the TLS
# key pair belong to a private directory outside the source tree.
if(RDPSERVER_CONFIG_DIR)
    if(NOT EXISTS "${RDPSERVER_CONFIG_DIR}/rdpserver.ini")
        message(FATAL_ERROR "RDPSERVER_CONFIG_DIR has no rdpserver.ini: ${RDPSERVER_CONFIG_DIR}")
    endif()
    file(GLOB _rdpserver_config LIST_DIRECTORIES false
        "${RDPSERVER_CONFIG_DIR}/rdpserver.ini"
        "${RDPSERVER_CONFIG_DIR}/*.crt"
        "${RDPSERVER_CONFIG_DIR}/*.key"
        "${RDPSERVER_CONFIG_DIR}/*.pem")
else()
    set(_rdpserver_config "${CMAKE_CURRENT_SOURCE_DIR}/rdpserver.ini")
endif()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    ${_rdpserver_files} ${_rdpserver_modules} ${_rdpserver_licenses} ${_rdpserver_config})
add_cd_file(FILE ${_rdpserver_config} DESTINATION reactos/system32/rdpserver FOR livecd preinstall)

add_registry_inf(rdpserver_reg.inf)
