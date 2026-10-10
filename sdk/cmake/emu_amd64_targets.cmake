# PROJECT:     LiberNT Build System
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     Define the AMD64 guest runtime shipped beside the AMD64 emulator
# COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>

set(EMU_AMD64_MODULES
    advapi32
    kernel32
    kernel32_vista
    kernelbase
    msvcrt
    ntdll
    ntdll_vista
    rpcrt4
    sechost)

set(EMU_AMD64_EXECUTABLES
    cmd
    emulation_test
    hostname)
