# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Ahmed ARIF <arif193@gmail.com>

set(ROSCONFIG_PROFILE_PACKAGES
    applertkit
    applepcie
    appledwc3
    applepd
    applemtp
    appleaopsensor
    applebatt
    appleaop
    applesmc
    applepmp
    applecpufreq
    applefpwm
    appledcp
    appleagx
    appleaopmic
    appleaudio
    appleisp
    bcmpcibt
    cyw43455)
set(ROSCONFIG_PROFILE_CONFIGS
    "APPLE_SILICON_SUPPORT:BOOL=ON"
    "NINTENDO_SWITCH_SUPPORT:BOOL=OFF"
    "RPI_SUPPORT:BOOL=OFF"
    "RPI3_SUPPORT:BOOL=OFF"
    "RPI5_SUPPORT:BOOL=OFF")
