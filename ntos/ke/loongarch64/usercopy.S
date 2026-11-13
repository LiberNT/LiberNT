/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 * PURPOSE:     LoongArch64 bounded user-copy leaves with explicit fixups
 *
 * The copies walk one byte at a time. That is deliberate: a fault must be
 * attributable to exactly one instruction (the trap handler compares ERA
 * with the label below), and the recovery model is "fail the whole copy",
 * never "resume halfway". A vectorised copy would have to reproduce the
 * same guarantee, so it is not done here.
 *
 * Only the marked user-side accesses may fault and recover:
 *   KiLa64CopyFromUserRaw   loads from the user buffer   -> KiLa64CopyFromUserFault
 *   KiLa64CopyToUserRaw     stores into the user buffer   -> KiLa64CopyToUserFault
 *   KiLa64ReadKernelRaw     reads resident kernel metadata -> KiLa64ReadKernelFault
 * A fault on the kernel buffer is a kernel bug and stays fatal, which is why
 * the kernel-side access carries no label.
 */

    .text
    .balign 4
    .global KiLa64CopyFromUserRaw
    .type   KiLa64CopyFromUserRaw, @function
/* NTSTATUS KiLa64CopyFromUserRaw(PVOID Destination, const VOID *Source, SIZE_T Length) */
KiLa64CopyFromUserRaw:
    beqz    $a2, .Lcopy_done
.Lcopy_from_user:
    .global KiLa64CopyFromUserFault
KiLa64CopyFromUserFault:
    ld.bu   $t0, $a1, 0
    st.b    $t0, $a0, 0
    addi.d  $a0, $a0, 1
    addi.d  $a1, $a1, 1
    addi.d  $a2, $a2, -1
    bnez    $a2, .Lcopy_from_user
.Lcopy_done:
    move    $a0, $zero
    jr      $ra
    .size   KiLa64CopyFromUserRaw, . - KiLa64CopyFromUserRaw

    .balign 4
    .global KiLa64CopyToUserRaw
    .type   KiLa64CopyToUserRaw, @function
/* NTSTATUS KiLa64CopyToUserRaw(PVOID Destination, const VOID *Source, SIZE_T Length) */
KiLa64CopyToUserRaw:
    beqz    $a2, .Lcopy_done
.Lcopy_to_user:
    ld.bu   $t0, $a1, 0
    .global KiLa64CopyToUserFault
KiLa64CopyToUserFault:
    st.b    $t0, $a0, 0
    addi.d  $a0, $a0, 1
    addi.d  $a1, $a1, 1
    addi.d  $a2, $a2, -1
    bnez    $a2, .Lcopy_to_user
    b       .Lcopy_done
    .size   KiLa64CopyToUserRaw, . - KiLa64CopyToUserRaw

    .balign 4
    .global KiLa64CopyUserFailure
    .type   KiLa64CopyUserFailure, @function
/* The trap fixup redirects ERA here with the failure NTSTATUS in saved $a0. */
KiLa64CopyUserFailure:
    jr      $ra
    .size   KiLa64CopyUserFailure, . - KiLa64CopyUserFailure

    .balign 4
    .global KiLa64ReadKernelRaw
    .type   KiLa64ReadKernelRaw, @function
/* NTSTATUS KiLa64ReadKernelRaw(PVOID Destination, const VOID *Source, SIZE_T Length)
 *
 * The resident kernel metadata reader: only the source load is recoverable,
 * because the destination is a kernel buffer the caller has already proved. */
KiLa64ReadKernelRaw:
    beqz    $a2, .Lcopy_done
.Lread_kernel:
    .global KiLa64ReadKernelFault
KiLa64ReadKernelFault:
    ld.bu   $t0, $a1, 0
    st.b    $t0, $a0, 0
    addi.d  $a0, $a0, 1
    addi.d  $a1, $a1, 1
    addi.d  $a2, $a2, -1
    bnez    $a2, .Lread_kernel
    b       .Lcopy_done
    .size   KiLa64ReadKernelRaw, . - KiLa64ReadKernelRaw
