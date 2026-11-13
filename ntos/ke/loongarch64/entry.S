/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 * PURPOSE:     LoongArch64 kernel handoff entry
 *
 * The loader enters the kernel in mapped mode (CRMD.PG=1, CRMD.DA=0) with
 * $sp = the top of a resident direct-map kernel stack and $a0 = the
 * LOADER_PARAMETER_BLOCK, an equally direct-mapped address; see
 * docs/loongarch-handoff-contract.md D-1/D-6 and section 2.2. Nothing may
 * be relied on beyond that: $r21 is not yet the KPCR base (the C consumer
 * establishes it), and no vector exists.
 *
 * This stub is deliberately the whole of the assembly: it proves the entry
 * precondition that can be proven from a register (CRMD.IE clear) by
 * clearing it again idempotently with csrxchg, and hands the loader block
 * to KiLa64SystemStartup, which validates everything else against the live
 * CSRs before it publishes any kernel state.
 */

#include "asmoffsets.h"

    .section .text$KiSystemStartup, "ax", @progbits
    .balign 4
    .global KiSystemStartup
    .type   KiSystemStartup, @function
KiSystemStartup:
    /* Only CRMD.IE is written; PG, DA, DATF and DATM are left exactly as the
     * loader set them ([V1-7.4.1]: an illegal {DA,PG} pair is undefined). */
    li.d    $t0, 0
    li.d    $t1, 0x4
    csrxchg $t0, $t1, LA64_CSR_CRMD
    dbar    0
    b       KiLa64SystemStartup

    /* KiLa64SystemStartup never returns. Fail stop if a compiler or calling
     * convention change ever breaks that contract. */
.Lstartup_returned:
    idle    0
    b       .Lstartup_returned
    .size   KiSystemStartup, . - KiSystemStartup
