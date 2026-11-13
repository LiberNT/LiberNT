/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     KDBG helper to switch to the debugger stack and call a routine
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

    .text
    .balign 4
    .globl KdbpStackSwitchAndCall
    .type  KdbpStackSwitchAndCall, @function

/*
 * VOID NTAPI KdbpStackSwitchAndCall(PVOID NewStack, VOID (*Function)(VOID))
 *   $a0 = NewStack (top of the debugger stack; aligned to 16 here)
 *   $a1 = Function
 *
 * The original $sp is kept in $s1 (callee-saved) across the call. A psABI
 * frame record ([fp-8] = $ra, [fp-16] = $fp) is pushed on the new stack so
 * the frame-pointer chain a KD back trace follows stays intact.
 */
KdbpStackSwitchAndCall:
    addi.d  $sp, $sp, -32
    st.d    $ra, $sp, 24
    st.d    $fp, $sp, 16
    st.d    $s0, $sp, 8
    st.d    $s1, $sp, 0
    addi.d  $fp, $sp, 32

    move    $s1, $sp
    /* andi cannot take a negative immediate; clear the low four bits. */
    srli.d  $t0, $a0, 4
    slli.d  $t0, $t0, 4
    move    $sp, $t0

    addi.d  $sp, $sp, -32
    st.d    $ra, $sp, 24
    st.d    $fp, $sp, 16
    addi.d  $fp, $sp, 32

    jirl    $ra, $a1, 0

    ld.d    $fp, $sp, 16
    ld.d    $ra, $sp, 24

    move    $sp, $s1
    ld.d    $s1, $sp, 0
    ld.d    $s0, $sp, 8
    ld.d    $fp, $sp, 16
    ld.d    $ra, $sp, 24
    addi.d  $sp, $sp, 32
    jr      $ra
    .size   KdbpStackSwitchAndCall, . - KdbpStackSwitchAndCall

/* EOF */
