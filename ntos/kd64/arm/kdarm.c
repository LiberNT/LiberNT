/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     ARMv7 KD architecture support
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* ARM has no model-specific registers and no port space for KD to reach. */
NTSTATUS NTAPI KdpSysReadMsr(_In_ ULONG Msr, _Out_ PULONGLONG MsrValue)
{
    UNREFERENCED_PARAMETER(Msr);
    UNREFERENCED_PARAMETER(MsrValue);
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS NTAPI KdpSysWriteMsr(_In_ ULONG Msr, _In_ PULONGLONG MsrValue)
{
    UNREFERENCED_PARAMETER(Msr);
    UNREFERENCED_PARAMETER(MsrValue);
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS NTAPI KdpSysReadIoSpace(_In_ INTERFACE_TYPE InterfaceType, _In_ ULONG BusNumber, _In_ ULONG AddressSpace,
                                 _In_ ULONG64 IoAddress, _Out_ PVOID DataValue, _In_ ULONG DataSize,
                                 _Out_ PULONG ActualDataSize)
{
    UNREFERENCED_PARAMETER(InterfaceType);
    UNREFERENCED_PARAMETER(BusNumber);
    UNREFERENCED_PARAMETER(AddressSpace);
    UNREFERENCED_PARAMETER(IoAddress);
    UNREFERENCED_PARAMETER(DataValue);
    UNREFERENCED_PARAMETER(DataSize);
    *ActualDataSize = 0;
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS NTAPI KdpSysWriteIoSpace(_In_ INTERFACE_TYPE InterfaceType, _In_ ULONG BusNumber, _In_ ULONG AddressSpace,
                                  _In_ ULONG64 IoAddress, _In_ PVOID DataValue, _In_ ULONG DataSize,
                                  _Out_ PULONG ActualDataSize)
{
    UNREFERENCED_PARAMETER(InterfaceType);
    UNREFERENCED_PARAMETER(BusNumber);
    UNREFERENCED_PARAMETER(AddressSpace);
    UNREFERENCED_PARAMETER(IoAddress);
    UNREFERENCED_PARAMETER(DataValue);
    UNREFERENCED_PARAMETER(DataSize);
    *ActualDataSize = 0;
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS NTAPI KdpSysCheckLowMemory(_In_ ULONG Flags)
{
    UNREFERENCED_PARAMETER(Flags);
    return STATUS_NOT_SUPPORTED;
}

VOID
NTAPI
KdpGetStateChange(
    _Inout_ PDBGKD_MANIPULATE_STATE64 State,
    _Inout_ PCONTEXT Context)
{
    UNREFERENCED_PARAMETER(Context);

    if (NT_SUCCESS(State->u.Continue2.ContinueStatus) &&
        (State->u.Continue2.ControlSet.CurrentSymbolStart != 1))
    {
        KdpCurrentSymbolStart = (ULONG_PTR)State->u.Continue2.ControlSet.CurrentSymbolStart;
        KdpCurrentSymbolEnd = (ULONG_PTR)State->u.Continue2.ControlSet.CurrentSymbolEnd;
    }
}

VOID
NTAPI
KdpSetContextState(
    _Inout_ PDBGKD_ANY_WAIT_STATE_CHANGE WaitStateChange,
    _Inout_ PCONTEXT Context)
{
    WaitStateChange->ControlReport.Cpsr = Context->Cpsr;
}

NTSTATUS
NTAPI
KdpAllowDisable(VOID)
{
    /* No hardware breakpoint is programmed on ARM yet. */
    return STATUS_SUCCESS;
}
