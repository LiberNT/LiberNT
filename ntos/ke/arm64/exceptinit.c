/*
 * PROJECT:         LiberNT Kernel (ARM64)
 * LICENSE:         GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * FILE:            ntoskrnl/ke/arm64/exceptinit.c
 * PURPOSE:         Final exception/interrupt initialization stubs for ARM64
 * COPYRIGHT:       Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

BOOLEAN KiArm64FinalVectorsInstalled = FALSE;
BOOLEAN KiArm64SvcConfigured = FALSE;
BOOLEAN KiArm64IrqFiqConfigured = FALSE;
ULONG64 KiArm64BootSErrorDisr = 0;

extern const UINT64 KiArm64VectorTable[];

CODE_SEG("INIT")
VOID
NTAPI
KeInitExceptions(VOID)
{
    UINT64 Vbar = (UINT64)(ULONG_PTR)&KiArm64VectorTable;
    UINT64 Pfr0;
    UINT64 Disr;

    /* Program the permanent vector base and advertise readiness */
    ARM64_WRITE_SYSREG(VBAR_EL1, Vbar);
    ARM64_ISB();

    Pfr0 = ARM64_READ_SYSREG(ID_AA64PFR0_EL1);
    if (((Pfr0 >> 28) & 0xF) != 0)
    {
        ARM64_DSB(SY);
        ARM64_HINT(16);
        ARM64_ISB();
        Disr = ARM64_READ_SYSREG(S3_0_C12_C1_1);
        if (Disr & (1ULL << 31))
        {
            KiArm64BootSErrorDisr = Disr;
#if defined(_MSC_VER) && !defined(__clang__)
            ARM64_WRITE_SYSREG(S3_0_C12_C1_1, 0);
#else
            __asm__ __volatile__("msr s3_0_c12_c1_1, xzr");
#endif
            ARM64_ISB();
        }
    }

    KiArm64FinalVectorsInstalled = TRUE;
    KiArm64SvcConfigured = TRUE;
    KiArm64IrqFiqConfigured = TRUE;
}
