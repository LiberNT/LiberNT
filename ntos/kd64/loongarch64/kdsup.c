/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     LoongArch64 KD architecture support
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* LoongArch64 has no MSRs, no port space and no bus cycles the kernel can
 * reach through the debugger: there is no CPUID/MSR analogue (CPUCFG is a
 * fixed table, not a read/write namespace), I/O is memory mapped, and
 * configuration space is reached through the HAL's ECAM window, not an
 * architectural bus access. None of these may fabricate answer data. */
#define LA64_KD_UNAVAILABLE(Name, Parameters) \
    NTSTATUS NTAPI Name Parameters { return STATUS_NOT_SUPPORTED; }

LA64_KD_UNAVAILABLE(KdpSysReadMsr, (ULONG Msr, PULONGLONG MsrValue))
LA64_KD_UNAVAILABLE(KdpSysWriteMsr, (ULONG Msr, PULONGLONG MsrValue))
LA64_KD_UNAVAILABLE(KdpSysReadBusData, (BUS_DATA_TYPE BusDataType, ULONG BusNumber, ULONG SlotNumber, ULONG Offset, PVOID Buffer, ULONG Length, PULONG ActualLength))
LA64_KD_UNAVAILABLE(KdpSysWriteBusData, (BUS_DATA_TYPE BusDataType, ULONG BusNumber, ULONG SlotNumber, ULONG Offset, PVOID Buffer, ULONG Length, PULONG ActualLength))
LA64_KD_UNAVAILABLE(KdpSysReadIoSpace, (INTERFACE_TYPE InterfaceType, ULONG BusNumber, ULONG AddressSpace, ULONG64 IoAddress, PVOID DataValue, ULONG DataSize, PULONG ActualDataSize))
LA64_KD_UNAVAILABLE(KdpSysWriteIoSpace, (INTERFACE_TYPE InterfaceType, ULONG BusNumber, ULONG AddressSpace, ULONG64 IoAddress, PVOID DataValue, ULONG DataSize, PULONG ActualDataSize))
LA64_KD_UNAVAILABLE(KdpSysCheckLowMemory, (ULONG Flags))

VOID
NTAPI
KdpGetStateChange(
    _Inout_ PDBGKD_MANIPULATE_STATE64 State,
    _Inout_ PCONTEXT Context)
{
    UNREFERENCED_PARAMETER(Context);

    /* Only process successful continue requests */
    if (NT_SUCCESS(State->u.Continue2.ContinueStatus))
    {
        /* Update current symbol window if debugger sent new values */
        if (State->u.Continue2.ControlSet.CurrentSymbolStart != 1)
        {
            KdpCurrentSymbolStart = (ULONG_PTR)State->u.Continue2.ControlSet.CurrentSymbolStart;
            KdpCurrentSymbolEnd = (ULONG_PTR)State->u.Continue2.ControlSet.CurrentSymbolEnd;
        }
    }
}

VOID
NTAPI
KdpSetContextState(
    _Inout_ PDBGKD_ANY_WAIT_STATE_CHANGE WaitStateChange,
    _Inout_ PCONTEXT Context)
{
    ULONG Index;

    /* KdpSetCommonState already copied the instruction window and count.
     * This port's control report carries the whole GPR bank plus the PCs, so
     * the debugger sees the stopped frame exactly as it was captured. */
    for (Index = 0; Index < RTL_NUMBER_OF(WaitStateChange->ControlReport.Gpr); Index++)
        WaitStateChange->ControlReport.Gpr[Index] = Context->R[Index];
    WaitStateChange->ControlReport.Era = Context->Pc;
}

NTSTATUS
NTAPI
KdpAllowDisable(VOID)
{
    /* No hardware watchpoint or single-step state can be left armed: the
     * LoongArch64 debug registers are not driven by this port yet, so
     * disabling the debugger cannot leave one behind. */
    return STATUS_SUCCESS;
}

/* Kernel calls already execute in the debugger's address space. User calls
 * enter through the private SYSCALL service and use the same KD workers. */
ULONG
NTAPI
DebugService(
    _In_ ULONG Service,
    _In_ PVOID Argument1,
    _In_ PVOID Argument2,
    _In_ PVOID Argument3,
    _In_ PVOID Argument4)
{
    BOOLEAN Handled = FALSE;

    switch (Service)
    {
        case BREAKPOINT_PRINT:
            return (ULONG)KdpPrint((ULONG)(ULONG_PTR)Argument3,
                                   (ULONG)(ULONG_PTR)Argument4,
                                   (PCHAR)Argument1,
                                   (USHORT)(ULONG_PTR)Argument2,
                                   KernelMode,
                                   NULL,
                                   NULL,
                                   &Handled);

        case BREAKPOINT_PROMPT:
            return (ULONG)KdpPrompt((PCHAR)Argument1,
                                    (USHORT)(ULONG_PTR)Argument2,
                                    (PCHAR)Argument3,
                                    (USHORT)(ULONG_PTR)Argument4,
                                    KernelMode,
                                    NULL,
                                    NULL);

        default:
            break;
    }

    return (ULONG)STATUS_NOT_IMPLEMENTED;
}

VOID
NTAPI
DebugService2(
    _In_ PVOID Argument1,
    _In_ PVOID Argument2,
    _In_ ULONG Service)
{
    CONTEXT Context;

    if (KdDebuggerNotPresent) return;
    RtlCaptureContext(&Context);
    switch (Service)
    {
        case BREAKPOINT_LOAD_SYMBOLS:
        case BREAKPOINT_UNLOAD_SYMBOLS:
            KdpSymbol((PSTRING)Argument1, (PKD_SYMBOLS_INFO)Argument2,
                      Service == BREAKPOINT_UNLOAD_SYMBOLS, KernelMode,
                      &Context, NULL, NULL);
            break;
        case BREAKPOINT_COMMAND_STRING:
            KdpCommandString((PSTRING)Argument1, (PSTRING)Argument2,
                             KernelMode, &Context, NULL, NULL);
            break;
        default:
            break;
    }
}

/* EOF */
