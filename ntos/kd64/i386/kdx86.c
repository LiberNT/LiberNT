/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     i386 kernel debugger target: context, control space, MSRs and I/O space
 * COPYRIGHT:   Copyright Alex Ionescu <alex.ionescu@reactos.org>
 *              Copyright Stefan Ginsberg <stefan.ginsberg@reactos.org>
 *              Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

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
    }
}

VOID
NTAPI
KdpSetContextState(
    _In_ PDBGKD_ANY_WAIT_STATE_CHANGE WaitStateChange,
    _In_ PCONTEXT Context)
{
    PKPRCB Prcb = KeGetCurrentPrcb();

    /* Copy i386 specific debug registers */
    WaitStateChange->ControlReport.Dr6 = Prcb->ProcessorState.SpecialRegisters.
                                         KernelDr6;
    WaitStateChange->ControlReport.Dr7 = Prcb->ProcessorState.SpecialRegisters.
                                         KernelDr7;

    WaitStateChange->ControlReport.SegCs = (USHORT)Context->SegCs;
    WaitStateChange->ControlReport.SegDs = (USHORT)Context->SegDs;
    WaitStateChange->ControlReport.SegEs = (USHORT)Context->SegEs;
    WaitStateChange->ControlReport.SegFs = (USHORT)Context->SegFs;

    WaitStateChange->ControlReport.EFlags = Context->EFlags;

    WaitStateChange->ControlReport.ReportFlags = REPORT_INCLUDES_SEGS;
    if (WaitStateChange->ControlReport.SegCs == KGDT_R0_CODE)
    {
        WaitStateChange->ControlReport.ReportFlags |= REPORT_STANDARD_CS;
    }
}

NTSTATUS
NTAPI
KdpSysReadControlSpace(
    _In_ ULONG Processor,
    _In_ ULONG64 BaseAddress,
    _Out_writes_bytes_(Length) PVOID Buffer,
    _In_ ULONG Length,
    _Out_ PULONG ActualLength)
{
    PVOID ControlStart;
    ULONG RealLength;

    if ((BaseAddress < sizeof(KPROCESSOR_STATE)) &&
        (Processor < KeNumberProcessors))
    {
        RealLength = sizeof(KPROCESSOR_STATE) - (ULONG_PTR)BaseAddress;
        if (RealLength < Length) Length = RealLength;

        ControlStart = (PVOID)((ULONG_PTR)BaseAddress +
                               (ULONG_PTR)&KiProcessorBlock[Processor]->
                                           ProcessorState);

        return KdpCopyMemoryChunks((ULONG_PTR)Buffer,
                                   ControlStart,
                                   Length,
                                   0,
                                   MMDBG_COPY_UNSAFE | MMDBG_COPY_WRITE,
                                   ActualLength);
    }
    else
    {
        *ActualLength = 0;
        return STATUS_UNSUCCESSFUL;
    }
}

NTSTATUS
NTAPI
KdpSysWriteControlSpace(
    _In_ ULONG Processor,
    _In_ ULONG64 BaseAddress,
    _In_reads_bytes_(Length) PVOID Buffer,
    _In_ ULONG Length,
    _Out_ PULONG ActualLength)
{
    PVOID ControlStart;

    if (((BaseAddress + Length) <= sizeof(KPROCESSOR_STATE)) &&
        (Processor < KeNumberProcessors))
    {
        ControlStart = (PVOID)((ULONG_PTR)BaseAddress +
                               (ULONG_PTR)&KiProcessorBlock[Processor]->
                                           ProcessorState);

        return KdpCopyMemoryChunks((ULONG_PTR)Buffer,
                                   ControlStart,
                                   Length,
                                   0,
                                   MMDBG_COPY_UNSAFE,
                                   ActualLength);
    }
    else
    {
        *ActualLength = 0;
        return STATUS_UNSUCCESSFUL;
    }
}

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
    NTSTATUS Status;

    if ((InterfaceType != Isa) || (BusNumber != 0) || (AddressSpace != 1))
    {
        *ActualDataSize = 0;
        return STATUS_UNSUCCESSFUL;
    }

    switch (DataSize)
    {
        case sizeof(UCHAR):
        {
            *(PUCHAR)DataValue =
                READ_PORT_UCHAR((PUCHAR)(ULONG_PTR)IoAddress);
            *ActualDataSize = sizeof(UCHAR);
            Status = STATUS_SUCCESS;
            break;
        }

        case sizeof(USHORT):
        {
            if ((IoAddress & (sizeof(USHORT) - 1)) != 0)
            {
                *ActualDataSize = 0;
                Status = STATUS_DATATYPE_MISALIGNMENT;
                break;
            }

            *(PUSHORT)DataValue =
                READ_PORT_USHORT((PUSHORT)(ULONG_PTR)IoAddress);
            *ActualDataSize = sizeof(USHORT);
            Status = STATUS_SUCCESS;
            break;
        }

        case sizeof(ULONG):
        {
            if ((IoAddress & (sizeof(ULONG) - 1)) != 0)
            {
                *ActualDataSize = 0;
                Status = STATUS_DATATYPE_MISALIGNMENT;
                break;
            }

            *(PULONG)DataValue =
                READ_PORT_ULONG((PULONG)(ULONG_PTR)IoAddress);
            *ActualDataSize = sizeof(ULONG);
            Status = STATUS_SUCCESS;
            break;
        }

        default:

            *ActualDataSize = 0;
            Status = STATUS_INVALID_PARAMETER;
    }

    return Status;
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
    NTSTATUS Status;

    if ((InterfaceType != Isa) || (BusNumber != 0) || (AddressSpace != 1))
    {
        *ActualDataSize = 0;
        return STATUS_UNSUCCESSFUL;
    }

    switch (DataSize)
    {
        case sizeof(UCHAR):
        {
            WRITE_PORT_UCHAR((PUCHAR)(ULONG_PTR)IoAddress,
                             *(PUCHAR)DataValue);
            *ActualDataSize = sizeof(UCHAR);
            Status = STATUS_SUCCESS;
            break;
        }

        case sizeof(USHORT):
        {
            if ((IoAddress & (sizeof(USHORT) - 1)) != 0)
            {
                *ActualDataSize = 0;
                Status = STATUS_DATATYPE_MISALIGNMENT;
                break;
            }

            WRITE_PORT_USHORT((PUSHORT)(ULONG_PTR)IoAddress,
                             *(PUSHORT)DataValue);
            *ActualDataSize = sizeof(USHORT);
            Status = STATUS_SUCCESS;
            break;
        }

        case sizeof(ULONG):
        {
            if ((IoAddress & (sizeof(ULONG) - 1)) != 0)
            {
                *ActualDataSize = 0;
                Status = STATUS_DATATYPE_MISALIGNMENT;
                break;
            }

            WRITE_PORT_ULONG((PULONG)(ULONG_PTR)IoAddress,
                             *(PULONG)DataValue);
            *ActualDataSize = sizeof(ULONG);
            Status = STATUS_SUCCESS;
            break;
        }

        default:

            *ActualDataSize = 0;
            Status = STATUS_INVALID_PARAMETER;
    }

    return Status;
}

NTSTATUS
NTAPI
KdpSysCheckLowMemory(
    _In_ ULONG Flags)
{
    return STATUS_UNSUCCESSFUL;
}
