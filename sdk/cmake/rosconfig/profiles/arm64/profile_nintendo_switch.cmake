# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Ahmed ARIF <arif193@gmail.com>

set(ROSCONFIG_PROFILE_PACKAGES
    tegragpio
    tegrausbphy)
set(ROSCONFIG_PROFILE_CONFIGS
    "NINTENDO_SWITCH_SUPPORT:BOOL=ON"
    # The system runs from a RAM disk and has no storage driver, so a log file
    # would not survive the boot it describes; the console's own panel does.
    "BOOTLOADER_DEBUG_PORT:STRING=SCREEN"
    "RPI_SUPPORT:BOOL=OFF"
    "RPI3_SUPPORT:BOOL=OFF"
    "RPI5_SUPPORT:BOOL=OFF"
    "APPLE_SILICON_SUPPORT:BOOL=OFF"
    "ENABLE_WOW64:BOOL=OFF"
    "ENABLE_FEX_ARM64EC:BOOL=OFF"
    "ENABLE_FFMPEG:BOOL=OFF"
    "ENABLE_MESA_LLVMPIPE:BOOL=OFF"
    "ENABLE_ROSAPPS:BOOL=OFF"
    "ENABLE_ROSTESTS:BOOL=OFF"
    "PREINSTALL_WINE_MONO:BOOL=OFF")
