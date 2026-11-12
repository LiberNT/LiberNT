/*
 * PROJECT:     LiberNT Apple RTKit Library
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Mailbox and RTKit management protocol of Apple silicon coprocessors
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#include <reactos/drivers/fdtdrv.h>

#define RTKIT_EP_MGMT           0x00
#define RTKIT_EP_CRASHLOG       0x01
#define RTKIT_EP_SYSLOG         0x02
#define RTKIT_EP_DEBUG          0x03
#define RTKIT_EP_IOREPORT       0x04
#define RTKIT_EP_OSLOG          0x08
#define RTKIT_EP_APP_START      0x20

#define RTKIT_PWR_STATE_OFF       0x00
#define RTKIT_PWR_STATE_SLEEP     0x01
#define RTKIT_PWR_STATE_QUIESCED  0x10
#define RTKIT_PWR_STATE_ON        0x20
#define RTKIT_PWR_STATE_INIT      0x220

#define APPLE_RTKIT_QUEUE_SLOTS         256
#define APPLE_RTKIT_FALLBACK_POLL_MS    1000
#define APPLE_RTKIT_NO_POLL             MAXULONG

typedef struct _APPLE_RTKIT APPLE_RTKIT, *PAPPLE_RTKIT;
typedef struct _APPLE_RTKIT_INTERRUPT APPLE_RTKIT_INTERRUPT, *PAPPLE_RTKIT_INTERRUPT;

/* Provide a buffer the coprocessor asked for. Iova is zero when the host chooses the address. */
typedef BOOLEAN (NTAPI *PAPPLE_RTKIT_SHMEM_SETUP)(
    _In_ PVOID Context,
    _In_ UCHAR Endpoint,
    _In_ ULONG Size,
    _Inout_ PULONG64 Iova,
    _Out_ PVOID *Virtual,
    _Out_ PBOOLEAN Preassigned);

/* A message for an application endpoint (0x20 and above). */
typedef VOID (NTAPI *PAPPLE_RTKIT_RECEIVE)(
    _In_ PVOID Context,
    _In_ UCHAR Endpoint,
    _In_ ULONG64 Message);

struct _APPLE_RTKIT
{
    PCSTR Name;
    PUCHAR Mailbox;
    PVOID Context;
    PAPPLE_RTKIT_SHMEM_SETUP ShmemSetup;
    PAPPLE_RTKIT_RECEIVE Receive;

    ULONG Version;
    ULONG IopState;
    ULONG ApState;
    BOOLEAN EpmapDone;
    BOOLEAN IopAcked;
    BOOLEAN ApAcked;
    BOOLEAN Failed;
    BOOLEAN CrashlogSeen;
    ULONG Endpoints[8];

    PVOID SyslogBuffer;
    ULONG SyslogBufferSize;
    ULONG SyslogEntries;
    ULONG SyslogMessageSize;

    PVOID CrashlogBuffer;
    ULONG CrashlogBufferSize;

    struct _KINTERRUPT *Interrupt;
    PAPPLE_RTKIT_INTERRUPT InterruptState;
    volatile LONG Interrupts;
    ULONG QueueHead;
    ULONG QueueTail;
    ULONG QueueLost;
    ULONG QueueLostReported;
    UCHAR QueueEndpoint[APPLE_RTKIT_QUEUE_SLOTS];
    ULONG64 QueueMessage[APPLE_RTKIT_QUEUE_SLOTS];
};

#define APPLE_RTKIT_CRASH_STACK     8

typedef struct _APPLE_RTKIT_CRASH
{
    CHAR Version[64];
    CHAR Message[192];
    BOOLEAN Registers;
    ULONG64 Pc;
    ULONG64 Lr;
    ULONG64 Sp;
    ULONG64 Psr;
    ULONG64 Far;
    ULONG64 Esr;
    ULONG StackDepth;
    ULONG64 Stack[APPLE_RTKIT_CRASH_STACK];
} APPLE_RTKIT_CRASH, *PAPPLE_RTKIT_CRASH;

typedef struct _APPLE_RTKIT_BOOT_ARGUMENT
{
    ULONG Key;
    ULONG64 Value;
} APPLE_RTKIT_BOOT_ARGUMENT, *PAPPLE_RTKIT_BOOT_ARGUMENT;

#define APPLE_RTKIT_FOURCC(A, B, C, D) \
    (((ULONG)(UCHAR)(A) << 24) | ((ULONG)(UCHAR)(B) << 16) | ((ULONG)(UCHAR)(C) << 8) | (ULONG)(UCHAR)(D))

BOOLEAN
NTAPI
AppleRtkitPatchBootArgumentBlock(
    _Inout_updates_bytes_(Size) PUCHAR Block,
    _In_ ULONG Size,
    _In_reads_(Count) const APPLE_RTKIT_BOOT_ARGUMENT *Patches,
    _In_ ULONG Count,
    _Out_opt_ PULONG Patched);

NTSTATUS
NTAPI
AppleRtkitPatchBootArguments(
    _In_ PUCHAR Memory,
    _In_ ULONG MemorySize,
    _In_ ULONG OffsetRegister,
    _In_ ULONG SizeRegister,
    _In_reads_(Count) const APPLE_RTKIT_BOOT_ARGUMENT *Patches,
    _In_ ULONG Count,
    _In_z_ PCSTR Name);

BOOLEAN
NTAPI
AppleRtkitParseCrashlog(
    _In_reads_bytes_(Size) const VOID *Log,
    _In_ ULONG Size,
    _Out_ PAPPLE_RTKIT_CRASH Crash);

BOOLEAN
NTAPI
AppleRtkitSend(
    _In_ PAPPLE_RTKIT Rtkit,
    _In_ UCHAR Endpoint,
    _In_ ULONG64 Message);

BOOLEAN
NTAPI
AppleRtkitPoll(
    _Inout_ PAPPLE_RTKIT Rtkit);

BOOLEAN
NTAPI
AppleRtkitHasEndpoint(
    _In_ PAPPLE_RTKIT Rtkit,
    _In_ UCHAR Endpoint);

VOID
NTAPI
AppleRtkitStartEndpoint(
    _Inout_ PAPPLE_RTKIT Rtkit,
    _In_ UCHAR Endpoint);

NTSTATUS
NTAPI
AppleRtkitWake(
    _Inout_ PAPPLE_RTKIT Rtkit,
    _In_ ULONG TimeoutMs);

NTSTATUS
NTAPI
AppleRtkitBoot(
    _Inout_ PAPPLE_RTKIT Rtkit,
    _In_ ULONG TimeoutMs);

NTSTATUS
NTAPI
AppleRtkitSetPowerState(
    _Inout_ PAPPLE_RTKIT Rtkit,
    _In_ ULONG ApState,
    _In_ ULONG IopState,
    _In_ ULONG TimeoutMs);

NTSTATUS
NTAPI
AppleRtkitQueryInterrupt(
    _In_ PFDT_DRIVER_TREE Tree,
    _In_ ULONG Node,
    _Out_ PULONG Gsi,
    _Out_ PBOOLEAN EdgeTriggered);

ULONG
NTAPI
AppleRtkitDrainMailbox(
    _Inout_ PAPPLE_RTKIT Rtkit);

ULONG
NTAPI
AppleRtkitPollInterval(
    _In_ PAPPLE_RTKIT Rtkit,
    _In_ ULONG PollMs);

#ifndef UNIT_TEST
NTSTATUS
NTAPI
AppleRtkitConnectInterrupt(
    _Inout_ PAPPLE_RTKIT Rtkit,
    _In_opt_ PCM_RESOURCE_LIST Raw,
    _In_opt_ PCM_RESOURCE_LIST Translated,
    _In_ ULONG Gsi,
    _In_opt_ PKEVENT Notify);

VOID
NTAPI
AppleRtkitDisconnectInterrupt(
    _Inout_ PAPPLE_RTKIT Rtkit);

NTSTATUS
NTAPI
AppleAddInterruptRequirement(
    _Inout_ PIRP Irp,
    _In_ ULONG Gsi,
    _In_ BOOLEAN EdgeTriggered);
#endif
