/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 * PURPOSE:     LoongArch64 system-service invocation
 *
 * ReactOS-private convention (there is no Windows LoongArch64 ABI to copy):
 * the service id travels in $t0 and the first eight arguments in $a0-$a7,
 * with any further arguments on the user stack. The C dispatcher has already
 * copied the whole argument vector into kernel memory, so this stub only
 * has to place it where the callee will look for it: eight register
 * arguments and slots 8..31 in the outgoing stack-argument area at $sp + 0,
 * which is where a callee's incoming stack arguments start for this psABI
 * (16-byte aligned stack, first stack argument at offset 0).
 */

    .text
    .balign 4
    .global KiLa64InvokeSystemService
    .type   KiLa64InvokeSystemService, @function
/* ULONG_PTR KiLa64InvokeSystemService(PVOID Routine, PULONG_PTR Arguments) */
KiLa64InvokeSystemService:
    addi.d  $sp, $sp, -208                    /* 24 slots + saved $ra */
    st.d    $ra, $sp, 200
    move    $t0, $a0                          /* Routine */
    move    $t1, $a1                          /* Arguments */

    .set    slot, 0
    .rept   24
    ld.d    $t2, $t1, (64 + slot * 8)
    st.d    $t2, $sp, (slot * 8)
    .set    slot, slot + 1
    .endr

    .set    slot, 0
    .irp    reg,a0,a1,a2,a3,a4,a5,a6,a7
    ld.d    $\reg, $t1, (slot * 8)
    .set    slot, slot + 1
    .endr

    jirl    $ra, $t0, 0

    ld.d    $ra, $sp, 200
    addi.d  $sp, $sp, 208
    jr      $ra
    .size   KiLa64InvokeSystemService, . - KiLa64InvokeSystemService
