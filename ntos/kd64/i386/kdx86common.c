/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Kernel debugger target operations that i386 and amd64 share: MSRs and the disable check
 * COPYRIGHT:   Copyright Alex Ionescu <alex.ionescu@reactos.org>
 *              Copyright Stefan Ginsberg <stefan.ginsberg@reactos.org>
 *              Copyright Timo Kreuzer <timo.kreuzer@reactos.org>
 *              Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

NTSTATUS
NTAPI
KdpSysReadMsr(
    _In_ ULONG Msr,
    _Out_ PULONGLONG MsrValue)
{
    _SEH2_TRY
    {
        *MsrValue = __readmsr(Msr);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        _SEH2_YIELD(return STATUS_NO_SUCH_DEVICE);
    }
    _SEH2_END;

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
KdpSysWriteMsr(
    _In_ ULONG Msr,
    _In_ PULONGLONG MsrValue)
{
    _SEH2_TRY
    {
        __writemsr(Msr, *MsrValue);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        _SEH2_YIELD(return STATUS_NO_SUCH_DEVICE);
    }
    _SEH2_END;

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
KdpAllowDisable(VOID)
{
    ULONG i;

    for (i = 0; i < KeNumberProcessors; i++)
    {
        PKPROCESSOR_STATE ProcessorState = &KiProcessorBlock[i]->ProcessorState;

        /* Active processor breakpoints need a debugger, so it cannot be disabled */
        if (ProcessorState->SpecialRegisters.KernelDr7 & 0xFF)
            return STATUS_ACCESS_DENIED;
    }

    return STATUS_SUCCESS;
}

/* EOF */
