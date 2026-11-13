/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 * PURPOSE:     Explicit deferred LoongArch64 kernel operations
 */

#include <ntoskrnl.h>

/* Retain the first unavailable operation for an external debugger, including
 * failures before PCR or console initialization and recursive bugcheck
 * failure. */
const CHAR * volatile KiLa64UnimplementedRoutine;

DECLSPEC_NORETURN
VOID
NTAPI
KiLa64Unimplemented(_In_ const CHAR *Routine)
{
    KeDisableInterrupts();
    if (KiLa64UnimplementedRoutine == NULL)
    {
        KiLa64UnimplementedRoutine = Routine;
        if (KeNumberProcessors && KeGetPcr() &&
            KeGetCurrentPrcb()->Number < (ULONG)(UCHAR)KeNumberProcessors &&
            KeGetCurrentPrcb() == KiProcessorBlock[KeGetCurrentPrcb()->Number])
        {
            KeBugCheckEx(KMODE_EXCEPTION_NOT_HANDLED, STATUS_NOT_IMPLEMENTED, (ULONG_PTR)Routine, 0, 0);
        }
    }

    /* `idle 0` waits for an interrupt with CRMD.IE already clear, so nothing
     * can wake this core; a debugger attached to the halted core is the only
     * way out. */
    for (;;)
        __asm__ __volatile__("idle 0" ::: "memory");
}

/* Failure-returning services do not inspect input or modify output storage. */
NTSTATUS NTAPI NtVdmControl(ULONG ControlCode, PVOID ControlData) { return STATUS_NOT_SUPPORTED; }

NTSTATUS
NTAPI
NtSetLdtEntries(
    _In_ ULONG Selector1,
    _In_ LDT_ENTRY LdtEntry1,
    _In_ ULONG Selector2,
    _In_ LDT_ENTRY LdtEntry2)
{
    UNREFERENCED_PARAMETER(Selector1);
    UNREFERENCED_PARAMETER(LdtEntry1);
    UNREFERENCED_PARAMETER(Selector2);
    UNREFERENCED_PARAMETER(LdtEntry2);
    return STATUS_NOT_IMPLEMENTED;
}
