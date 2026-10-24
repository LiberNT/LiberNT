/*
 * PROJECT:     LiberNT HAL
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     PSCI SMC and HVC conduit calls for MSVC builds
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

    .text

    .balign 4
    .global HalpArm64PsciSmc
HalpArm64PsciSmc:
    smc     #0
    ret

    .balign 4
    .global HalpArm64PsciHvc
HalpArm64PsciHvc:
    hvc     #0
    ret
