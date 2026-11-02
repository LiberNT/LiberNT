# PROJECT:     LiberNT RDP console server
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     Build the RDP console server against the in-tree FreeRDP and OpenSSL
# COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif193@gmail.com>

add_executable(rdpserver rdpserver.c transfer.c)
target_compile_definitions(rdpserver PRIVATE __STDC_NO_THREADS__=1)
if(NOT MSVC)
    target_compile_options(rdpserver PRIVATE -std=gnu11)
endif()
if(CMAKE_C_COMPILER_ID STREQUAL "Clang")
    target_compile_options(rdpserver PRIVATE -fno-autolink)
endif()
target_link_libraries(rdpserver freerdp-server freerdp winpr libssl libcrypto uuid oldnames)
if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
    target_link_libraries(rdpserver libgcc_eh)
endif()
set_module_type(rdpserver win32cui)
add_importlibs(rdpserver advapi32 crypt32 dbghelp gdi32 ncrypt ole32 rpcrt4 secur32 shell32 shlwapi user32 winmm ws2_32 msvcrt kernel32 ntdll)
add_cd_file(TARGET rdpserver DESTINATION reactos/system32/rdpserver FOR all)
add_cd_file(FILE ${REACTOS_SOURCE_DIR}/sdk/lib/3rdparty/freerdp/LICENSE DESTINATION reactos/system32/rdpserver/licenses NAME_ON_CD FreeRDP-LICENSE FOR all)
add_cd_file(FILE ${REACTOS_SOURCE_DIR}/sdk/lib/3rdparty/openssl/LICENSE.txt DESTINATION reactos/system32/rdpserver/licenses NAME_ON_CD OpenSSL-LICENSE FOR all)

# A private rdpserver.ini, such as one with a file transfer root, replaces the in-tree one.
if(RDPSERVER_CONFIG_DIR)
    if(NOT EXISTS "${RDPSERVER_CONFIG_DIR}/rdpserver.ini")
        message(FATAL_ERROR "RDPSERVER_CONFIG_DIR has no rdpserver.ini: ${RDPSERVER_CONFIG_DIR}")
    endif()
    set(_rdpserver_config "${RDPSERVER_CONFIG_DIR}/rdpserver.ini")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${_rdpserver_config})
else()
    set(_rdpserver_config "${CMAKE_CURRENT_SOURCE_DIR}/rdpserver.ini")
endif()
add_cd_file(FILE ${_rdpserver_config} DESTINATION reactos/system32/rdpserver FOR all)

add_registry_inf(rdpserver_reg.inf)
