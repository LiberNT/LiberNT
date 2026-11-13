/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 * PURPOSE:     LoongArch64 native kernel-stack migration
 *
 * KeSwitchKernelStack is entered from the trap entry when a win32k service
 * arrives on a stack that cannot hold it, before any C frame exists. Only
 * the psABI frame record ($fp chain) and the trap frame need relocating, and
 * KiLa64PrepareKernelStack does that; this stub moves $sp only after the C
 * helper returns, so nothing in the helper ever runs on a half-moved stack.
 */

#include "asmoffsets.h"

    .section .text$KeSwitchKernelStack, "ax", @progbits
    .balign 4
    .global KeSwitchKernelStack
    .type   KeSwitchKernelStack, @function
/* PVOID KeSwitchKernelStack(PVOID StackBase, PVOID StackLimit)
 *
 * Returns the old allocation base. $fp is the frame register (r22), the same
 * [fp-8] = caller's $ra / [fp-16] = caller's $fp layout GCC and Clang emit
 * for this target when a frame pointer is forced. */
KeSwitchKernelStack:
    addi.d  $sp, $sp, -48
    st.d    $ra, $sp, 40
    st.d    $fp, $sp, 32
    addi.d  $fp, $sp, 48
    st.d    $a0, $sp, 0
    /* A guarded region masks APC delivery, not clock preemption. */
    li.d    $t0, 0
    li.d    $t1, 0x4
    csrxchg $t0, $t1, LA64_CSR_CRMD
    andi    $t0, $t0, 0x4
    st.d    $t0, $sp, 16
    /* a0/a1 are the new base and limit; a2/a3 describe this stub's frame. */
    addi.d  $a2, $sp, 0
    move    $a3, $fp
    bl      KiLa64PrepareKernelStack

    /* a0 is the old allocation base, returned unchanged to the caller. */
    ld.d    $t1, $sp, 0
    sub.d   $t1, $t1, $a0
    add.d     $sp, $sp, $t1
    ld.d    $t0, $sp, 16
    ld.d    $ra, $sp, 40
    /* The copied frame record already contains the relocated caller $fp. */
    ld.d    $fp, $sp, 32
    addi.d  $sp, $sp, 48
    beqz    $t0, .Lstack_no_interrupts
    li.d    $t1, 0x4
    csrxchg $t1, $t1, LA64_CSR_CRMD
.Lstack_no_interrupts:
    jr      $ra
    .size   KeSwitchKernelStack, . - KeSwitchKernelStack
