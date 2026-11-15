/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     amd64 kernel debugger target: context, control space, MSRs and I/O space
 * COPYRIGHT:   Copyright Timo Kreuzer <timo.kreuzer@reactos.org>
 *              Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

ULONG
NTAPI
HalpKdReadPciConfig(
    _In_ ULONG BusNumber,
    _In_ ULONG SlotNumber,
    _Out_writes_bytes_(Length) PVOID Buffer,
    _In_ ULONG Offset,
    _In_ ULONG Length);

static
VOID
KdpCaptureLegacyFloatingState(
    _Inout_ PCONTEXT Context)
{
    DECLSPEC_ALIGN(16) XMM_SAVE_AREA32 FloatingState;

    _fxsave64(&FloatingState);
    KdpMoveMemory(&Context->FltSave,
                  &FloatingState,
                  FIELD_OFFSET(XMM_SAVE_AREA32, XmmRegisters));
}

static
VOID
KdpRestoreLegacyFloatingState(
    _In_ PCONTEXT Context)
{
    DECLSPEC_ALIGN(16) XMM_SAVE_AREA32 FloatingState;

    /*
     * Preserve the debugger's live SSE state while applying only the x87
     * fields exposed through the KD context.
     */
    _fxsave64(&FloatingState);
    KdpMoveMemory(&FloatingState,
                  &Context->FltSave,
                  FIELD_OFFSET(XMM_SAVE_AREA32, XmmRegisters));
    _fxrstor64(&FloatingState);
}

VOID
NTAPI
KdpGetStateChange(
    _In_ PDBGKD_MANIPULATE_STATE64 State,
    _In_ PCONTEXT Context)
{
    PKPRCB Prcb;
    ULONG i;

    if (NT_SUCCESS(State->u.Continue2.ContinueStatus))
    {
        if (State->u.Continue2.ControlSet.TraceFlag)
        {
            Context->EFlags |= EFLAGS_TF;
        }
        else
        {
            Context->EFlags &= ~EFLAGS_TF;
        }

        for (i = 0; i < KeNumberProcessors; i++)
        {
            Prcb = KiProcessorBlock[i];
            Prcb->ProcessorState.SpecialRegisters.KernelDr7 =
                State->u.Continue2.ControlSet.Dr7;
            Prcb->ProcessorState.SpecialRegisters.KernelDr6 = 0;
        }

        if (State->u.Continue2.ControlSet.CurrentSymbolStart != 1)
        {
            KdpCurrentSymbolStart =
                State->u.Continue2.ControlSet.CurrentSymbolStart;
            KdpCurrentSymbolEnd= State->u.Continue2.ControlSet.CurrentSymbolEnd;
        }

        KdpRestoreLegacyFloatingState(Context);
    }
}

VOID
NTAPI
KdpSetContextState(
    _In_ PDBGKD_ANY_WAIT_STATE_CHANGE WaitStateChange,
    _In_ PCONTEXT Context)
{
    KdpCaptureLegacyFloatingState(Context);

    WaitStateChange->ControlReport.Dr6 = Context->Dr6;
    WaitStateChange->ControlReport.Dr7 = Context->Dr7;

    WaitStateChange->ControlReport.SegCs = (USHORT)Context->SegCs;
    WaitStateChange->ControlReport.SegDs = (USHORT)Context->SegDs;
    WaitStateChange->ControlReport.SegEs = (USHORT)Context->SegEs;
    WaitStateChange->ControlReport.SegFs = (USHORT)Context->SegFs;

    WaitStateChange->ControlReport.EFlags = Context->EFlags;

    WaitStateChange->ControlReport.ReportFlags = REPORT_INCLUDES_SEGS;
    if (WaitStateChange->ControlReport.SegCs == KGDT64_R0_CODE)
    {
        WaitStateChange->ControlReport.ReportFlags |= REPORT_STANDARD_CS;
    }
}

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
KdpSysReadBusData(
    _In_ BUS_DATA_TYPE BusDataType,
    _In_ ULONG BusNumber,
    _In_ ULONG SlotNumber,
    _In_ ULONG Offset,
    _Out_writes_bytes_(Length) PVOID Buffer,
    _In_ ULONG Length,
    _Out_ PULONG ActualLength)
{
    ULONG Read;

    /* A processor frozen by NMI may hold the PCI configuration lock, so a stopped machine never waits on it */
    if (KdEnteredDebugger && (BusDataType == PCIConfiguration))
    {
        Read = HalpKdReadPciConfig(BusNumber, SlotNumber, Buffer, Offset, Length);
        if (Read == MAXULONG)
        {
            *ActualLength = 0;
            return STATUS_DEVICE_BUSY;
        }
    }
    else
    {
        Read = HalGetBusDataByOffset(BusDataType, BusNumber, SlotNumber, Buffer, Offset, Length);
    }

    *ActualLength = Read;
    return (Read != 0) ? STATUS_SUCCESS : STATUS_UNSUCCESSFUL;
}

NTSTATUS
NTAPI
KdpSysWriteBusData(
    _In_ BUS_DATA_TYPE BusDataType,
    _In_ ULONG BusNumber,
    _In_ ULONG SlotNumber,
    _In_ ULONG Offset,
    _In_reads_bytes_(Length) PVOID Buffer,
    _In_ ULONG Length,
    _Out_ PULONG ActualLength)
{
    /* FIXME: on a stopped machine this waits for a PCI configuration lock a frozen processor may hold */
    *ActualLength = HalSetBusDataByOffset(BusDataType,
                                          BusNumber,
                                          SlotNumber,
                                          Buffer,
                                          Offset,
                                          Length);

    return (*ActualLength != 0) ? STATUS_SUCCESS : STATUS_UNSUCCESSFUL;
}

/* KdpSysReadControlSpace/KdpSysWriteControlSpace live in kd64/kdcontrol.c,
 * shared with ARM64 (same control-space layout and constants). */

NTSTATUS
NTAPI
KdpSysReadIoSpace(
    _In_ INTERFACE_TYPE InterfaceType,
    _In_ ULONG BusNumber,
    _In_ ULONG AddressSpace,
    _In_ ULONG64 IoAddress,
    _Out_writes_bytes_(DataSize) PVOID DataValue,
    _In_ ULONG DataSize,
    _Out_ PULONG ActualDataSize)
{
    if (InterfaceType != Isa || BusNumber != 0 || AddressSpace != 1)
    {
        *ActualDataSize = 0;
        return STATUS_INVALID_PARAMETER;
    }

    if ((IoAddress & (DataSize - 1)))
    {
        *ActualDataSize = 0;
        return STATUS_DATATYPE_MISALIGNMENT;
    }

    switch (DataSize)
    {
        case sizeof(UCHAR):

            *(PUCHAR)DataValue = READ_PORT_UCHAR((PUCHAR)IoAddress);
            break;

        case sizeof(USHORT):

            *(PUSHORT)DataValue = READ_PORT_USHORT((PUSHORT)IoAddress);
            break;

        case sizeof(ULONG):

            *(PULONG)DataValue = READ_PORT_ULONG((PULONG)IoAddress);
            break;

        default:

            *ActualDataSize = 0;
            return STATUS_INVALID_PARAMETER;
    }

    *ActualDataSize = DataSize;

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
KdpSysWriteIoSpace(
    _In_ INTERFACE_TYPE InterfaceType,
    _In_ ULONG BusNumber,
    _In_ ULONG AddressSpace,
    _In_ ULONG64 IoAddress,
    _In_reads_bytes_(DataSize) PVOID DataValue,
    _In_ ULONG DataSize,
    _Out_ PULONG ActualDataSize)
{
    if (InterfaceType != Isa || BusNumber != 0 || AddressSpace != 1)
    {
        *ActualDataSize = 0;
        return STATUS_INVALID_PARAMETER;
    }

    if ((IoAddress & (DataSize - 1)))
    {
        *ActualDataSize = 0;
        return STATUS_DATATYPE_MISALIGNMENT;
    }

    switch (DataSize)
    {
        case sizeof(UCHAR):

            WRITE_PORT_UCHAR((PUCHAR)IoAddress, *(PUCHAR)DataValue);
            break;

        case sizeof(USHORT):

            WRITE_PORT_USHORT((PUSHORT)IoAddress, *(PUSHORT)DataValue);
            break;

        case sizeof(ULONG):

            WRITE_PORT_ULONG((PULONG)IoAddress, *(PULONG)DataValue);
            break;

        default:

            *ActualDataSize = 0;
            return STATUS_INVALID_PARAMETER;
    }

    *ActualDataSize = DataSize;

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
KdpSysCheckLowMemory(
    _In_ ULONG Flags)
{
    UNREFERENCED_PARAMETER(Flags);

    /* /NOLOWMEM is not honoured, so no pattern below 4 GB can have been corrupted */
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
