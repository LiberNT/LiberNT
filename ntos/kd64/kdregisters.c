/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     What the host reads besides memory: registers, ports and buses
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#include "kdpriv.h"
#define NDEBUG
#include <debug.h>

/* PUBLIC FUNCTIONS ***********************************************************/

/*
 * Who this kernel is, where it starts, and which parts of the protocol it
 * speaks. Everything the host does afterwards depends on this answer, so it
 * is handed out from one place.
 */
VOID
NTAPI
KdpSysGetVersion(
    _Out_ PDBGKD_GET_VERSION64 Version)
{
    KdpMoveMemory(Version, &KdVersionBlock, sizeof(DBGKD_GET_VERSION64));
}

/*
 * The one request that must be answered before the host knows anything.
 */
VOID
NTAPI
KdpGetVersion(
    _Inout_ PDBGKD_MANIPULATE_STATE64 State)
{
    KdpSysGetVersion(&State->u.GetVersion64);
    State->ReturnStatus = STATUS_SUCCESS;

    KdpSendManipulateReply(State, NULL);
}

/*
 * Hands the host the registers of the processor it is talking to, as they were
 * when the machine stopped.
 */
VOID
NTAPI
KdpGetContext(
    _Inout_ PDBGKD_MANIPULATE_STATE64 State,
    _Inout_ PSTRING Data,
    _In_ PCONTEXT Context)
{
    if ((State->Processor >= (USHORT)(CCHAR)KeNumberProcessors) ||
        (Data->MaximumLength < sizeof(CONTEXT)))
    {
        State->ReturnStatus = STATUS_UNSUCCESSFUL;
        Data->Length = 0;
        KdpSendManipulateReply(State, Data);
        return;
    }

    KdpMoveMemory(Data->Buffer, Context, sizeof(CONTEXT));
    Data->Length = sizeof(CONTEXT);
    State->ReturnStatus = STATUS_SUCCESS;

    /* The host now has what it needs to decide where to continue from */
    KdpContextSent = TRUE;

    KdpSendManipulateReply(State, Data);
}

/*
 * Takes the registers the host wants this processor to run with.
 */
VOID
NTAPI
KdpSetContext(
    _Inout_ PDBGKD_MANIPULATE_STATE64 State,
    _In_ PSTRING Data,
    _In_ PCONTEXT Context)
{
    if ((State->Processor >= (USHORT)(CCHAR)KeNumberProcessors) ||
        (Data->Length < sizeof(CONTEXT)))
    {
        State->ReturnStatus = STATUS_UNSUCCESSFUL;
        KdpSendManipulateReply(State, NULL);
        return;
    }

    KdpMoveMemory(Context, Data->Buffer, sizeof(CONTEXT));
    State->ReturnStatus = STATUS_SUCCESS;

    KdpSendManipulateReply(State, NULL);
}

/*
 * The same registers, read a window at a time, for a host that wants only
 * part of a context that has grown larger than one packet.
 */
VOID
NTAPI
KdpGetContextEx(
    _Inout_ PDBGKD_MANIPULATE_STATE64 State,
    _Inout_ PSTRING Data,
    _In_ PCONTEXT Context)
{
    PDBGKD_CONTEXT_EX ContextEx = &State->u.ContextEx;

    if ((State->Processor >= (USHORT)(CCHAR)KeNumberProcessors) ||
        (ContextEx->Offset > sizeof(CONTEXT)) ||
        (ContextEx->ByteCount > sizeof(CONTEXT) - ContextEx->Offset) ||
        (ContextEx->ByteCount > Data->MaximumLength))
    {
        ContextEx->BytesCopied = 0;
        Data->Length = 0;
        State->ReturnStatus = STATUS_UNSUCCESSFUL;
        KdpSendManipulateReply(State, Data);
        return;
    }

    KdpMoveMemory(Data->Buffer,
                  (PVOID)((ULONG_PTR)Context + ContextEx->Offset),
                  ContextEx->ByteCount);

    ContextEx->BytesCopied = ContextEx->ByteCount;
    Data->Length = (USHORT)ContextEx->ByteCount;
    State->ReturnStatus = STATUS_SUCCESS;

    KdpSendManipulateReply(State, Data);
}

VOID
NTAPI
KdpSetContextEx(
    _Inout_ PDBGKD_MANIPULATE_STATE64 State,
    _In_ PSTRING Data,
    _In_ PCONTEXT Context)
{
    PDBGKD_CONTEXT_EX ContextEx = &State->u.ContextEx;

    if ((State->Processor >= (USHORT)(CCHAR)KeNumberProcessors) ||
        (ContextEx->Offset > sizeof(CONTEXT)) ||
        (ContextEx->ByteCount > sizeof(CONTEXT) - ContextEx->Offset) ||
        (ContextEx->ByteCount > Data->Length))
    {
        ContextEx->BytesCopied = 0;
        State->ReturnStatus = STATUS_UNSUCCESSFUL;
        KdpSendManipulateReply(State, NULL);
        return;
    }

    KdpMoveMemory((PVOID)((ULONG_PTR)Context + ContextEx->Offset),
                  Data->Buffer,
                  ContextEx->ByteCount);

    ContextEx->BytesCopied = ContextEx->ByteCount;
    State->ReturnStatus = STATUS_SUCCESS;

    KdpSendManipulateReply(State, NULL);
}

/*
 * Control space is whatever the architecture keeps per processor that is not
 * a register: the architecture answers for its own.
 */
VOID
NTAPI
KdpReadControlSpace(
    _Inout_ PDBGKD_MANIPULATE_STATE64 State,
    _Inout_ PSTRING Data,
    _In_ PCONTEXT Context)
{
    ULONG Length = State->u.ReadMemory.TransferCount;
    ULONG Actual = 0;

    UNREFERENCED_PARAMETER(Context);

    if (Length > Data->MaximumLength)
        Length = Data->MaximumLength;

    State->ReturnStatus = KdpSysReadControlSpace(State->Processor,
                                                 State->u.ReadMemory.TargetBaseAddress,
                                                 Data->Buffer,
                                                 Length,
                                                 &Actual);

    State->u.ReadMemory.ActualBytesRead = Actual;
    Data->Length = (USHORT)Actual;

    KdpSendManipulateReply(State, Data);
}

VOID
NTAPI
KdpWriteControlSpace(
    _Inout_ PDBGKD_MANIPULATE_STATE64 State,
    _In_ PSTRING Data,
    _In_ PCONTEXT Context)
{
    ULONG Length = State->u.WriteMemory.TransferCount;
    ULONG Actual = 0;

    UNREFERENCED_PARAMETER(Context);

    if (Length > Data->Length)
        Length = Data->Length;

    State->ReturnStatus = KdpSysWriteControlSpace(State->Processor,
                                                  State->u.WriteMemory.TargetBaseAddress,
                                                  Data->Buffer,
                                                  Length,
                                                  &Actual);

    State->u.WriteMemory.ActualBytesWritten = Actual;

    KdpSendManipulateReply(State, NULL);
}

/*
 * One port access on the host's behalf. The plain form speaks to the ISA
 * address space of bus zero; the extended form says which bus and space.
 */
VOID
NTAPI
KdpReadIoSpace(
    _Inout_ PDBGKD_MANIPULATE_STATE64 State,
    _In_ PSTRING Data,
    _In_ PCONTEXT Context,
    _In_ BOOLEAN Extended)
{
    INTERFACE_TYPE InterfaceType = Isa;
    ULONG BusNumber = 0;
    ULONG AddressSpace = 1;
    ULONG64 IoAddress;
    ULONG DataSize;
    ULONG Actual = 0;
    ULONG Value = 0;

    UNREFERENCED_PARAMETER(Data);
    UNREFERENCED_PARAMETER(Context);

    if (Extended)
    {
        InterfaceType = (INTERFACE_TYPE)State->u.ReadWriteIoExtended.InterfaceType;
        BusNumber = State->u.ReadWriteIoExtended.BusNumber;
        AddressSpace = State->u.ReadWriteIoExtended.AddressSpace;
        IoAddress = State->u.ReadWriteIoExtended.IoAddress;
        DataSize = State->u.ReadWriteIoExtended.DataSize;
    }
    else
    {
        IoAddress = State->u.ReadWriteIo.IoAddress;
        DataSize = State->u.ReadWriteIo.DataSize;
    }

    State->ReturnStatus = KdpSysReadIoSpace(InterfaceType,
                                            BusNumber,
                                            AddressSpace,
                                            IoAddress,
                                            &Value,
                                            DataSize,
                                            &Actual);

    if (Extended)
        State->u.ReadWriteIoExtended.DataValue = Value;
    else
        State->u.ReadWriteIo.DataValue = Value;

    KdpSendManipulateReply(State, NULL);
}

VOID
NTAPI
KdpWriteIoSpace(
    _Inout_ PDBGKD_MANIPULATE_STATE64 State,
    _In_ PSTRING Data,
    _In_ PCONTEXT Context,
    _In_ BOOLEAN Extended)
{
    INTERFACE_TYPE InterfaceType = Isa;
    ULONG BusNumber = 0;
    ULONG AddressSpace = 1;
    ULONG64 IoAddress;
    ULONG DataSize;
    ULONG Actual = 0;
    ULONG Value;

    UNREFERENCED_PARAMETER(Data);
    UNREFERENCED_PARAMETER(Context);

    if (Extended)
    {
        InterfaceType = (INTERFACE_TYPE)State->u.ReadWriteIoExtended.InterfaceType;
        BusNumber = State->u.ReadWriteIoExtended.BusNumber;
        AddressSpace = State->u.ReadWriteIoExtended.AddressSpace;
        IoAddress = State->u.ReadWriteIoExtended.IoAddress;
        DataSize = State->u.ReadWriteIoExtended.DataSize;
        Value = State->u.ReadWriteIoExtended.DataValue;
    }
    else
    {
        IoAddress = State->u.ReadWriteIo.IoAddress;
        DataSize = State->u.ReadWriteIo.DataSize;
        Value = State->u.ReadWriteIo.DataValue;
    }

    State->ReturnStatus = KdpSysWriteIoSpace(InterfaceType,
                                             BusNumber,
                                             AddressSpace,
                                             IoAddress,
                                             &Value,
                                             DataSize,
                                             &Actual);

    KdpSendManipulateReply(State, NULL);
}

VOID
NTAPI
KdpReadMachineSpecificRegister(
    _Inout_ PDBGKD_MANIPULATE_STATE64 State,
    _In_ PSTRING Data,
    _In_ PCONTEXT Context)
{
    ULONGLONG Value = 0;

    UNREFERENCED_PARAMETER(Data);
    UNREFERENCED_PARAMETER(Context);

    State->ReturnStatus = KdpSysReadMsr(State->u.ReadWriteMsr.Msr, &Value);

    State->u.ReadWriteMsr.DataValueLow = (ULONG)Value;
    State->u.ReadWriteMsr.DataValueHigh = (ULONG)(Value >> 32);

    KdpSendManipulateReply(State, NULL);
}

VOID
NTAPI
KdpWriteMachineSpecificRegister(
    _Inout_ PDBGKD_MANIPULATE_STATE64 State,
    _In_ PSTRING Data,
    _In_ PCONTEXT Context)
{
    ULONGLONG Value;

    UNREFERENCED_PARAMETER(Data);
    UNREFERENCED_PARAMETER(Context);

    Value = (ULONGLONG)State->u.ReadWriteMsr.DataValueLow |
            ((ULONGLONG)State->u.ReadWriteMsr.DataValueHigh << 32);

    State->ReturnStatus = KdpSysWriteMsr(State->u.ReadWriteMsr.Msr, &Value);

    KdpSendManipulateReply(State, NULL);
}

VOID
NTAPI
KdpGetBusData(
    _Inout_ PDBGKD_MANIPULATE_STATE64 State,
    _Inout_ PSTRING Data,
    _In_ PCONTEXT Context)
{
    ULONG Length = State->u.GetSetBusData.Length;
    ULONG Actual = 0;

    UNREFERENCED_PARAMETER(Context);

    if (Length > Data->MaximumLength)
        Length = Data->MaximumLength;

    State->ReturnStatus =
        KdpSysReadBusData((BUS_DATA_TYPE)State->u.GetSetBusData.BusDataType,
                          State->u.GetSetBusData.BusNumber,
                          State->u.GetSetBusData.SlotNumber,
                          State->u.GetSetBusData.Offset,
                          Data->Buffer,
                          Length,
                          &Actual);

    State->u.GetSetBusData.Length = Actual;
    Data->Length = (USHORT)Actual;

    KdpSendManipulateReply(State, Data);
}

VOID
NTAPI
KdpSetBusData(
    _Inout_ PDBGKD_MANIPULATE_STATE64 State,
    _In_ PSTRING Data,
    _In_ PCONTEXT Context)
{
    ULONG Length = State->u.GetSetBusData.Length;
    ULONG Actual = 0;

    UNREFERENCED_PARAMETER(Context);

    if (Length > Data->Length)
        Length = Data->Length;

    State->ReturnStatus =
        KdpSysWriteBusData((BUS_DATA_TYPE)State->u.GetSetBusData.BusDataType,
                           State->u.GetSetBusData.BusNumber,
                           State->u.GetSetBusData.SlotNumber,
                           State->u.GetSetBusData.Offset,
                           Data->Buffer,
                           Length,
                           &Actual);

    State->u.GetSetBusData.Length = Actual;

    KdpSendManipulateReply(State, NULL);
}

/*
 * What the machine would have counted had the debugger not held it. Code that
 * measures time across a breakpoint asks for this instead of the raw counter.
 */
LARGE_INTEGER
NTAPI
KdpQueryPerformanceCounter(VOID)
{
    LARGE_INTEGER Counter;

    Counter = KeQueryPerformanceCounter(NULL);
    Counter.QuadPart -= KdTimerDifference.QuadPart;

    return Counter;
}

/*
 * The answer to anything this kernel does not do. The host asks for several
 * things that only ever existed on other architectures, and expects a plain
 * refusal rather than silence.
 */
VOID
NTAPI
KdpNotSupported(
    _Inout_ PDBGKD_MANIPULATE_STATE64 State)
{
    State->ReturnStatus = STATUS_UNSUCCESSFUL;
    KdpSendManipulateReply(State, NULL);
}

/* EOF */
