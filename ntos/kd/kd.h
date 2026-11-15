/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Kernel debugger I/O provider declarations
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once
#include <cportlib/cportlib.h>

struct _KD_DISPATCH_TABLE;

#define KDP_BOOT_PHASE_BUSES 2

#define KDP_BOOT_PHASE_SYSTEM_ROOT 6

typedef
NTSTATUS
(NTAPI *PKDP_INIT_ROUTINE)(
    _In_ struct _KD_DISPATCH_TABLE *DispatchTable,
    _In_ ULONG BootPhase);

typedef
VOID
(NTAPI *PKDP_PRINT_ROUTINE)(
    _In_ PCCH String,
    _In_ ULONG Length);

extern PCONTEXT KdpReportedContext;

extern KPROCESSOR_MODE KdpReportedMode;

extern BOOLEAN KdpReportedFirstChance;

#ifdef KDBG
#define KdpKdbgInit KdbInitialize
#endif

#define KdScreen    0

#ifdef KDBG

#define KdMax       4
#else
#define KdMax       3
#endif

typedef struct _KDP_DEBUG_MODE
{
    union
    {
        struct
        {
            UCHAR Screen :1;
            UCHAR Serial :1;
            UCHAR File   :1;
            UCHAR Net    :1;
        };

        ULONG Value;
    };
} KDP_DEBUG_MODE;

typedef struct _KD_DISPATCH_TABLE
{
    LIST_ENTRY KdProvidersList;
    PKDP_INIT_ROUTINE KdpInitRoutine;
    PKDP_PRINT_ROUTINE KdpPrintRoutine;
    NTSTATUS InitStatus;
} KD_DISPATCH_TABLE, *PKD_DISPATCH_TABLE;

extern KDP_DEBUG_MODE KdpDebugMode;

extern ULONG  SerialPortNumber;

extern CPPORT SerialPortInfo;

extern PKDP_INIT_ROUTINE InitRoutines[KdMax];

extern KD_DISPATCH_TABLE DispatchTable[KdMax];

extern LIST_ENTRY KdProviders;

NTSTATUS
NTAPI
KdpSerialInit(
    _In_ struct _KD_DISPATCH_TABLE *DispatchTable,
    _In_ ULONG BootPhase);

VOID
KdpInfoInitialize(
    _In_ struct _LOADER_PARAMETER_BLOCK *LoaderBlock);

ULONG
NTAPI
KdpInfoState(VOID);

SIZE_T
KdIoReadLine(
    _Out_ PCHAR Buffer,
    _In_ SIZE_T Size);

VOID
KdpNetDisplayStatus(VOID);

BOOLEAN
KdpNetGetByte(
    _Out_ PUCHAR Byte);

NTSTATUS
NTAPI
KdpNetInit(
    _In_ struct _KD_DISPATCH_TABLE *DispatchTable,
    _In_ ULONG BootPhase);

BOOLEAN
KdpNetPollBreakIn(VOID);

BOOLEAN
KdpNetQueryAttached(
    _Out_opt_ PULONG Columns,
    _Out_opt_ PULONG Rows);

VOID
KdpNetSendCommand(
    _In_ PCSTR Command);

VOID
__cdecl
KdIoPrintf(
    _In_ PCSTR Format,
    ...);

VOID
KdIoPuts(
    _In_ PCSTR String);

BOOLEAN
NTAPI
KdbpAcquireLock(
    _In_ PKSPIN_LOCK SpinLock,
    _Out_ PKIRQL OldIrql);

VOID
NTAPI
KdbpReleaseLock(
    _In_ PKSPIN_LOCK SpinLock,
    _In_ KIRQL OldIrql,
    _In_ BOOLEAN LockAcquired);

NTSTATUS
NTAPI
KdpInitializeProviders(
    _In_ ULONG BootPhase);

VOID
NTAPI
KdpSelectDebugMode(
    _In_opt_ struct _LOADER_PARAMETER_BLOCK *LoaderBlock);

BOOLEAN
KdpScreenAcquire(VOID);

VOID
KdpScreenEnterBugCheck(
    _In_ BOOLEAN FollowPrints);

BOOLEAN
KdpScreenEnterStop(
    _In_ BOOLEAN Requested);

VOID
KdpScreenHoldOutput(
    _In_ BOOLEAN Hold);

NTSTATUS
NTAPI
KdpScreenInit(
    _In_ struct _KD_DISPATCH_TABLE *DispatchTable,
    _In_ ULONG BootPhase);

VOID
KdpScreenRelease(
    _In_ BOOLEAN DisplayAcquired);

ULONG
NTAPI
KdpInfoQuery(
    _In_ ULONG Class,
    _In_ ULONG Offset,
    _Out_writes_bytes_(Size) PUCHAR Buffer,
    _In_ ULONG Size,
    _Out_ PULONG Total);

VOID
KdpNetSendBytes(
    _In_reads_bytes_(Length) PCCH Buffer,
    _In_ ULONG Length);

VOID
NTAPI
KdpProvidersPrint(
    _In_reads_bytes_(Length) PCCH String,
    _In_ ULONG Length);
