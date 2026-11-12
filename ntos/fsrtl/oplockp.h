/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     What the oplock package keeps about a stream and who holds it
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#define FSRTL_OPLOCK_TAG 'pRsF'

/*
 * A grant is one promise the package has made. The legacy levels name
 * themselves; the Windows 7 levels are the cache flags the holder was told it
 * may keep, so losing a flag is the whole of a break.
 */
#define FSRTL_OPLOCK_LEVEL_1 0x00000001
#define FSRTL_OPLOCK_LEVEL_2 0x00000002
#define FSRTL_OPLOCK_BATCH 0x00000004
#define FSRTL_OPLOCK_FILTER 0x00000008
#define FSRTL_OPLOCK_CACHE 0x00000010

#define FSRTL_OPLOCK_LEGACY_EXCLUSIVE                                         \
    (FSRTL_OPLOCK_LEVEL_1 | FSRTL_OPLOCK_BATCH | FSRTL_OPLOCK_FILTER)

#define FSRTL_OPLOCK_CACHE_ALL                                                \
    (OPLOCK_LEVEL_CACHE_READ | OPLOCK_LEVEL_CACHE_WRITE |                     \
     OPLOCK_LEVEL_CACHE_HANDLE)

#define FSRTL_OPLOCK_STATE_BREAK_PENDING 0x00000001
#define FSRTL_OPLOCK_STATE_CLOSE_PENDING 0x00000002

#define FSRTL_WAIT_SYNC 0
#define FSRTL_WAIT_CALLBACK 1
#define FSRTL_WAIT_COMPLETE 2

typedef struct _FSRTL_OPLOCK_GRANT
{
    LIST_ENTRY Links;
    PIRP Irp;
    PFILE_OBJECT FileObject;
    GUID Key;
    ULONG Kind;
    ULONG CacheLevel;
    ULONG BrokenFrom;
    ULONG BrokenTo;
    BOOLEAN HasKey;
    BOOLEAN AckRequired;
    BOOLEAN CompleteOnClose;
} FSRTL_OPLOCK_GRANT, *PFSRTL_OPLOCK_GRANT;

typedef struct _FSRTL_OPLOCK_STATE
{
    FAST_MUTEX Mutex;
    LIST_ENTRY WaitingIrps;
    LIST_ENTRY SharedGrants;
    LIST_ENTRY BreakingGrants;
    PFSRTL_OPLOCK_GRANT ExclusiveGrant;
    ULONG Flags;
} FSRTL_OPLOCK_STATE, *PFSRTL_OPLOCK_STATE;

typedef struct _FSRTL_OPLOCK_WAIT
{
    LIST_ENTRY Links;
    PIRP Irp;
    PVOID Context;
    POPLOCK_WAIT_COMPLETE_ROUTINE CompletionRoutine;
    KEVENT Event;
    NTSTATUS Status;
    ULONG Mode;
} FSRTL_OPLOCK_WAIT, *PFSRTL_OPLOCK_WAIT;

PFSRTL_OPLOCK_STATE
NTAPI
FsRtlpAllocateOplock(
    _Inout_ POPLOCK Oplock);

VOID
NTAPI
FsRtlpCompleteOplockIrp(
    _In_ PIRP Irp,
    _In_ NTSTATUS Status,
    _In_ ULONG_PTR Information);

VOID
NTAPI
FsRtlpCompleteCollectedIrps(
    _Inout_ PLIST_ENTRY Collected);

VOID
NTAPI
FsRtlpReleaseWaiters(
    _Inout_ PLIST_ENTRY Released);

VOID
NTAPI
FsRtlpCollectWaiters(
    _Inout_ PFSRTL_OPLOCK_STATE State,
    _Inout_ PLIST_ENTRY Released,
    _In_ NTSTATUS Status);

NTSTATUS
NTAPI
FsRtlpWaitOnIrp(
    _Inout_ PFSRTL_OPLOCK_STATE State,
    _In_ PIRP Irp,
    _In_opt_ PVOID Context,
    _In_opt_ POPLOCK_WAIT_COMPLETE_ROUTINE CompletionRoutine,
    _In_opt_ POPLOCK_FS_PREPOST_IRP PostIrpRoutine,
    _In_ ULONG Mode,
    _Inout_opt_ PLIST_ENTRY Collected);

NTSTATUS
NTAPI
FsRtlpOplockFsctrlInternal(
    _Inout_ PFSRTL_OPLOCK_STATE State,
    _In_ PIRP Irp,
    _In_ ULONG OpenCount,
    _In_ ULONG Flags);

NTSTATUS
NTAPI
FsRtlpCheckOplockInternal(
    _In_ POPLOCK Oplock,
    _In_ PIRP Irp,
    _In_ ULONG Flags,
    _In_opt_ PVOID Context,
    _In_opt_ POPLOCK_WAIT_COMPLETE_ROUTINE CompletionRoutine,
    _In_opt_ POPLOCK_FS_PREPOST_IRP PostIrpRoutine,
    _In_ ULONG ForcedLoss);

NTSTATUS
NTAPI
FsRtlpOplockBreakToNoneInternal(
    _Inout_ POPLOCK Oplock,
    _In_ PIRP Irp,
    _In_ ULONG Flags,
    _In_opt_ PVOID Context,
    _In_opt_ POPLOCK_WAIT_COMPLETE_ROUTINE CompletionRoutine,
    _In_opt_ POPLOCK_FS_PREPOST_IRP PostIrpRoutine);

ULONG
NTAPI
FsRtlpHeldCacheLevel(
    _In_ PFSRTL_OPLOCK_STATE State);

BOOLEAN
NTAPI
FsRtlpOplockKeyFromIrp(
    _In_ PIRP Irp,
    _Out_ LPGUID Key);

BOOLEAN
NTAPI
FsRtlpOplockKeysEqual(
    _In_ PFSRTL_OPLOCK_GRANT Grant,
    _In_ BOOLEAN HasKey,
    _In_ LPCGUID Key);

/* EOF */
