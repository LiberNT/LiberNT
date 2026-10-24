/*
 * PROJECT:     LiberNT NT Virtual Memory Subsystem
 * FILE:        ntoskrnl/nvs/arch/arm64/tlbops.h
 * PURPOSE:     ARM64 translation lookaside buffer operation definitions
 *
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF
 * SPDX-License-Identifier: GPL-3.0-only
 */

#pragma once

#ifdef MM_HOST_TEST
VOID MiArm64TlbStoreBarrier(VOID);
VOID MiArm64TlbCompleteBarrier(VOID);
VOID MiArm64TlbInstructionBarrier(VOID);
VOID MiArm64TlbPage(ULONG64 Page, MI_TLB_SCOPE Scope);
VOID MiArm64TlbAll(MI_TLB_SCOPE Scope);
#else
FORCEINLINE
VOID MiArm64TlbStoreBarrier(VOID)
{
    ARM64_DSB(ISHST);
}

FORCEINLINE
VOID MiArm64TlbCompleteBarrier(VOID)
{
    ARM64_DSB(ISH);
}

FORCEINLINE
VOID MiArm64TlbInstructionBarrier(VOID)
{
    ARM64_ISB();
}

FORCEINLINE
VOID MiArm64TlbPage(ULONG64 Page, MI_TLB_SCOPE Scope)
{
    if (Scope == MiTlbAllProcessors)
        ARM64_TLBI(VAAE1IS, Page);
    else
        ARM64_TLBI(VAAE1, Page);
}

FORCEINLINE
VOID MiArm64TlbAll(MI_TLB_SCOPE Scope)
{
    if (Scope == MiTlbAllProcessors)
        ARM64_TLBI_ALL(VMALLE1IS);
    else
        ARM64_TLBI_ALL(VMALLE1);
}
#endif
