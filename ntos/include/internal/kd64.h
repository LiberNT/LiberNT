/*
 */

#pragma once

#define KD_DEFAULT_LOG_BUFFER_SIZE  0x20000

#define KDP_MSG_BUFFER_SIZE 0x1000

#define KD_BREAKPOINT_MAX   32

#define KD_BREAKPOINT_ACTIVE    0x01
#define KD_BREAKPOINT_PENDING   0x02
#define KD_BREAKPOINT_SUSPENDED 0x04
#define KD_BREAKPOINT_EXPIRED   0x08

typedef struct _BREAKPOINT_ENTRY
{
    ULONG Flags;
    ULONG_PTR DirectoryTableBase;
    PVOID Address;
    KD_BREAKPOINT_TYPE Content;
} BREAKPOINT_ENTRY, *PBREAKPOINT_ENTRY;

typedef
BOOLEAN
(NTAPI *PKDEBUG_ROUTINE)(
    IN PKTRAP_FRAME TrapFrame,
    IN PKEXCEPTION_FRAME ExceptionFrame,
    IN PEXCEPTION_RECORD ExceptionRecord,
    IN PCONTEXT Context,
    IN KPROCESSOR_MODE PreviousMode,
    IN BOOLEAN SecondChance
);








NTAPI

VOID
NTAPI

BOOLEAN
NTAPI

BOOLEAN
NTAPI

VOID
NTAPI

BOOLEAN
NTAPI











BOOLEAN
NTAPI

NTAPI

VOID
NTAPI

VOID
NTAPI

VOID
NTAPI

NTAPI

BOOLEAN
NTAPI

VOID
NTAPI

VOID
NTAPI

BOOLEAN
NTAPI

BOOLEAN
NTAPI

VOID
NTAPI

NTSTATUS
NTAPI

NTSTATUS
NTAPI

NTSTATUS
NTAPI
    _In_ KPROCESSOR_MODE PreviousMode,
    _In_ PKTRAP_FRAME TrapFrame,

VOID
NTAPI

BOOLEAN
NTAPI

NTAPI
    _In_ KPROCESSOR_MODE PreviousMode,
    _In_ PKTRAP_FRAME TrapFrame,

NTAPI


NTAPI

VOID
NTAPI

VOID

VOID
NTAPI

VOID
NTAPI

VOID
NTAPI

VOID
NTAPI

NTAPI

ULONG
NTAPI

NTAPI

BOOLEAN
NTAPI

BOOLEAN
NTAPI

NTAPI

NTAPI

VOID
NTAPI

NTAPI

NTAPI

NTAPI

VOID
NTAPI

VOID
NTAPI


VOID
NTAPI

NTAPI

NTAPI

NTAPI

NTAPI

NTSTATUS
NTAPI
KdpSysReadBusData(
    _In_ BUS_DATA_TYPE BusDataType,
    _In_ ULONG BusNumber,
    _In_ ULONG SlotNumber,
    _In_ ULONG Offset,
    _Out_writes_bytes_(Length) PVOID Buffer,
    _In_ ULONG Length,
    _Out_ PULONG ActualLength);

NTSTATUS
NTAPI
KdpSysReadControlSpace(
    _In_ ULONG Processor,
    _In_ ULONG64 BaseAddress,
    _Out_writes_bytes_(Length) PVOID Buffer,
    _In_ ULONG Length,
    _Out_ PULONG ActualLength);

NTSTATUS
NTAPI
KdpSysReadIoSpace(
    _In_ INTERFACE_TYPE InterfaceType,
    _In_ ULONG BusNumber,
    _In_ ULONG AddressSpace,
    _In_ ULONG64 IoAddress,
    _Out_writes_bytes_(DataSize) PVOID DataValue,
    _In_ ULONG DataSize,
    _Out_ PULONG ActualDataSize);

NTSTATUS
NTAPI
KdpSysWriteIoSpace(
    _In_ INTERFACE_TYPE InterfaceType,
    _In_ ULONG BusNumber,
    _In_ ULONG AddressSpace,
    _In_ ULONG64 IoAddress,
    _In_reads_bytes_(DataSize) PVOID DataValue,
    _In_ ULONG DataSize,
    _Out_ PULONG ActualDataSize);

NTSTATUS
NTAPI

NTAPI

FORCEINLINE
BOOLEAN
KdpDebuggerLockOwnedByCurrentThread(VOID)
{
#if DBG
    return (((KSPIN_LOCK)KeGetCurrentThread() | 1) == KdpDebuggerLock);
#else
    return FALSE;
#endif
}
