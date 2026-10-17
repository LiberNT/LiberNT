# PROJECT:     LiberNT RDP console server
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     Standalone MinGW build of rdpserver.exe against the FreeRDP SDK
# COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif193@gmail.com>

# Configure this project with an x86_64 MinGW toolchain against an installed
# FreeRDP 3 SDK; the LiberNT build stages its install directory through
# RDPSERVER_DIR (see ../README.md).
cmake_minimum_required(VERSION 3.22)
project(LiberNTRDPServer C)
find_package(WinPR 3 CONFIG REQUIRED)
find_package(FreeRDP 3 CONFIG REQUIRED)
find_package(FreeRDP-Server 3 CONFIG REQUIRED)
add_executable(rdpserver ../rdpserver.c ../transfer.c)
target_compile_definitions(rdpserver PRIVATE __STDC_NO_THREADS__=1 WINVER=0x0601 _WIN32_WINNT=0x0601)
if(MINGW)
    target_compile_options(rdpserver PRIVATE -Wall -Wextra -Wno-deprecated-declarations)
endif()
target_link_libraries(rdpserver PRIVATE freerdp-server freerdp winpr ws2_32 gdi32 user32 iphlpapi)
install(TARGETS rdpserver RUNTIME DESTINATION bin)
