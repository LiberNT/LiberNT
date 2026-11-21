##
## PROJECT:     LiberNT Build
## LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
## PURPOSE:     Raspberry Pi 3 in 32-bit mode (ARMv7) target profile
## COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
##

set(ROSCONFIG_PROFILE_PACKAGES
    sdbus
    usbdwc2
    smsc95xx)
set(ROSCONFIG_PROFILE_CONFIGS
    "RPI_SUPPORT:BOOL=ON")
