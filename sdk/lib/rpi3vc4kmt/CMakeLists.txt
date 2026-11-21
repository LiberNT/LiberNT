# PROJECT:     ReactOS Raspberry Pi 3 VC4 user-mode support
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# COPYRIGHT:   Copyright 2026 Ahmed Arif <arif193@gmail.com>

add_library(rpi3vc4kmt STATIC rpi3vc4kmt.c)
add_dependencies(rpi3vc4kmt psdk)
