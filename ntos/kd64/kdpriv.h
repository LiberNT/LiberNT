/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     What the kernel debugger engine says to itself
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

/*
 * The engine answers one request at a time out of this scratch space, so the
 * request body never has to live on the stack of whoever took the trap.
 */
#define KDP_RESPONSE_SIZE   PACKET_MAX_SIZE

#define KDP_MANIPULATE_HEADER_SIZE  sizeof(DBGKD_MANIPULATE_STATE64)

/* A retry budget of its own, so a dead link costs a bounded number of tries */
#define KDP_MAXIMUM_RETRIES 20

/* How long the time slip timer waits before it asks the host for the time */
#define KDP_TIME_SLIP_INTERVAL_MS   (-100 * 1000 * 10)

typedef struct _KDP_WAIT_RESULT
{
    KCONTINUE_STATUS Status;
    NTSTATUS ContinueStatus;
} KDP_WAIT_RESULT, *PKDP_WAIT_RESULT;

/* kdstartup.c */

BOOLEAN
NTAPI
KdRegisterDebuggerDataBlock(
    _In_ ULONG Tag,
    _In_ PDBGKD_DEBUG_DATA_HEADER64 DataHeader,
    _In_ ULONG Size);

/* kdengine.c */

VOID
NTAPI
KdpSetCommonState(
    _In_ ULONG NewState,
    _In_ PCONTEXT Context,
    _Out_ PDBGKD_ANY_WAIT_STATE_CHANGE WaitStateChange);

KCONTINUE_STATUS
NTAPI
KdpSendWaitContinue(
    _In_ ULONG PacketType,
    _In_ PSTRING MessageHeader,
    _In_opt_ PSTRING MessageData,
    _Inout_ PCONTEXT Context);

VOID
NTAPI
KdpSendManipulateReply(
    _In_ PDBGKD_MANIPULATE_STATE64 State,
    _In_opt_ PSTRING Data);

/* kdmemory.c */

VOID
NTAPI
KdpReadVirtualMemory(
    _Inout_ PDBGKD_MANIPULATE_STATE64 State,
    _Inout_ PSTRING Data,
    _In_ PCONTEXT Context);

VOID
NTAPI
KdpWriteVirtualMemory(
    _Inout_ PDBGKD_MANIPULATE_STATE64 State,
    _In_ PSTRING Data,
    _In_ PCONTEXT Context);

VOID
NTAPI
KdpReadPhysicalMemory(
    _Inout_ PDBGKD_MANIPULATE_STATE64 State,
    _Inout_ PSTRING Data,
    _In_ PCONTEXT Context);

VOID
NTAPI
KdpWritePhysicalMemory(
    _Inout_ PDBGKD_MANIPULATE_STATE64 State,
    _In_ PSTRING Data,
    _In_ PCONTEXT Context);

VOID
NTAPI
KdpSearchMemory(
    _Inout_ PDBGKD_MANIPULATE_STATE64 State,
    _Inout_ PSTRING Data,
    _In_ PCONTEXT Context);

VOID
NTAPI
KdpFillMemory(
    _Inout_ PDBGKD_MANIPULATE_STATE64 State,
    _In_ PSTRING Data,
    _In_ PCONTEXT Context);

VOID
NTAPI
KdpQueryMemory(
    _Inout_ PDBGKD_MANIPULATE_STATE64 State,
    _In_ PCONTEXT Context);

VOID
NTAPI
KdpCheckLowMemory(
    _Inout_ PDBGKD_MANIPULATE_STATE64 State);

/* kdregisters.c */

VOID
NTAPI
KdpGetVersion(
    _Inout_ PDBGKD_MANIPULATE_STATE64 State);

VOID
NTAPI
KdpGetContext(
    _Inout_ PDBGKD_MANIPULATE_STATE64 State,
    _Inout_ PSTRING Data,
    _In_ PCONTEXT Context);

VOID
NTAPI
KdpSetContext(
    _Inout_ PDBGKD_MANIPULATE_STATE64 State,
    _In_ PSTRING Data,
    _In_ PCONTEXT Context);

VOID
NTAPI
KdpGetContextEx(
    _Inout_ PDBGKD_MANIPULATE_STATE64 State,
    _Inout_ PSTRING Data,
    _In_ PCONTEXT Context);

VOID
NTAPI
KdpSetContextEx(
    _Inout_ PDBGKD_MANIPULATE_STATE64 State,
    _In_ PSTRING Data,
    _In_ PCONTEXT Context);

VOID
NTAPI
KdpReadControlSpace(
    _Inout_ PDBGKD_MANIPULATE_STATE64 State,
    _Inout_ PSTRING Data,
    _In_ PCONTEXT Context);

VOID
NTAPI
KdpWriteControlSpace(
    _Inout_ PDBGKD_MANIPULATE_STATE64 State,
    _In_ PSTRING Data,
    _In_ PCONTEXT Context);

VOID
NTAPI
KdpReadIoSpace(
    _Inout_ PDBGKD_MANIPULATE_STATE64 State,
    _In_ PSTRING Data,
    _In_ PCONTEXT Context,
    _In_ BOOLEAN Extended);

VOID
NTAPI
KdpWriteIoSpace(
    _Inout_ PDBGKD_MANIPULATE_STATE64 State,
    _In_ PSTRING Data,
    _In_ PCONTEXT Context,
    _In_ BOOLEAN Extended);

VOID
NTAPI
KdpReadMachineSpecificRegister(
    _Inout_ PDBGKD_MANIPULATE_STATE64 State,
    _In_ PSTRING Data,
    _In_ PCONTEXT Context);

VOID
NTAPI
KdpWriteMachineSpecificRegister(
    _Inout_ PDBGKD_MANIPULATE_STATE64 State,
    _In_ PSTRING Data,
    _In_ PCONTEXT Context);

VOID
NTAPI
KdpGetBusData(
    _Inout_ PDBGKD_MANIPULATE_STATE64 State,
    _Inout_ PSTRING Data,
    _In_ PCONTEXT Context);

VOID
NTAPI
KdpSetBusData(
    _Inout_ PDBGKD_MANIPULATE_STATE64 State,
    _In_ PSTRING Data,
    _In_ PCONTEXT Context);

LARGE_INTEGER
NTAPI
KdpQueryPerformanceCounter(VOID);

VOID
NTAPI
KdpNotSupported(
    _Inout_ PDBGKD_MANIPULATE_STATE64 State);

/* kdbreakpoint.c */

VOID
NTAPI
KdpWriteBreakpoint(
    _Inout_ PDBGKD_MANIPULATE_STATE64 State,
    _In_ PSTRING Data,
    _In_ PCONTEXT Context);

VOID
NTAPI
KdpRestoreBreakpoint(
    _Inout_ PDBGKD_MANIPULATE_STATE64 State,
    _In_ PSTRING Data,
    _In_ PCONTEXT Context);

/*
 * The two bulk forms both let the machine go on when they succeed, so they
 * say whether the conversation is over rather than only how it went.
 */
BOOLEAN
NTAPI
KdpWriteBreakPointEx(
    _Inout_ PDBGKD_MANIPULATE_STATE64 State,
    _In_ PSTRING Data,
    _Inout_ PCONTEXT Context);

BOOLEAN
NTAPI
KdpRestoreBreakPointEx(
    _Inout_ PDBGKD_MANIPULATE_STATE64 State,
    _In_ PSTRING Data,
    _In_ PCONTEXT Context);

BOOLEAN
NTAPI
KdpLowWriteContent(
    _In_ ULONG BpEntry);

BOOLEAN
NTAPI
KdpLowRestoreBreakpoint(
    _In_ ULONG BpEntry);

VOID
NTAPI
KdpWriteCustomBreakpoint(
    _Inout_ PDBGKD_MANIPULATE_STATE64 State,
    _In_ PSTRING Data,
    _In_ PCONTEXT Context);

/* kdenable.c */

VOID
NTAPI
KdpCauseBugCheck(
    _In_ PDBGKD_MANIPULATE_STATE64 State);

VOID
NTAPI
DumpTraceData(
    _Inout_ PSTRING TraceData);

/* kdfilter.c */

BOOLEAN
NTAPI
KdpFilterAllows(
    _In_ ULONG ComponentId,
    _In_ ULONG Level);

/* kdoutput.c */

VOID
NTAPI
KdpAcquireLock(
    _In_ PKSPIN_LOCK SpinLock,
    _Out_ PKIRQL OldIrql);

VOID
NTAPI
KdpReleaseLock(
    _In_ PKSPIN_LOCK SpinLock,
    _In_ KIRQL OldIrql);

/* The scratch space every request body and every reply body is built in */
extern CHAR KdpResponseBuffer[KDP_RESPONSE_SIZE];

/* EOF */
