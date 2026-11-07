/*
 * PROJECT:     LiberNT Apple RTKit Library
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Mailbox and RTKit management protocol of Apple silicon coprocessors
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

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

typedef struct _APPLE_RTKIT APPLE_RTKIT, *PAPPLE_RTKIT;

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
};

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
