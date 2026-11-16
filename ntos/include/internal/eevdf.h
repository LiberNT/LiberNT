/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Experimental earliest eligible virtual deadline first scheduling of the dynamic priorities
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

typedef struct _KI_EEVDF_ENTITY
{
    LIST_ENTRY DeadlineLink;
    ULONG64 Vruntime;
    ULONG64 Deadline;
    LONG64 Lag;
    ULONG64 ChargedCycleTime;
    ULONG64 LeftAt;
    ULONG Home;
    UCHAR Priority;
    BOOLEAN Listed;
    BOOLEAN SliceEnded;
} KI_EEVDF_ENTITY, *PKI_EEVDF_ENTITY;

typedef struct DECLSPEC_CACHEALIGN _KI_EEVDF_QUEUE
{
    LIST_ENTRY DeadlineList;
    LONG64 WeightedKeySum;
    ULONG64 Load;
    ULONG64 BaseVruntime;
    ULONG Count;
} KI_EEVDF_QUEUE, *PKI_EEVDF_QUEUE;

/* Every thread object carries its entity behind the ETHREAD, so no Windows structure holds scheduler state of ours */
#define KI_EEVDF_THREAD_EXTENSION sizeof(KI_EEVDF_ENTITY)

extern BOOLEAN KiEevdfEnabled;

CODE_SEG("INIT")
VOID
NTAPI
KeEnableEevdfScheduler(VOID);

_Requires_lock_held_(Prcb->PrcbLock)
VOID
FASTCALL
KiEevdfEnqueue(
    _Inout_ PKPRCB Prcb,
    _Inout_ PKTHREAD Thread,
    _In_ BOOLEAN Executing);

_Requires_lock_held_(Prcb->PrcbLock)
VOID
FASTCALL
KiEevdfUnlist(
    _Inout_ PKPRCB Prcb,
    _Inout_ PKTHREAD Thread);

_Requires_lock_held_(Prcb->PrcbLock)
PKTHREAD
FASTCALL
KiEevdfSelectReadyThread(
    _In_ KPRIORITY Priority,
    _Inout_ PKPRCB Prcb,
    _In_opt_ PKTHREAD Incumbent);

VOID
FASTCALL
KiEevdfDepart(
    _Inout_ PKTHREAD Thread,
    _In_ ULONG TargetProcessor);

_Requires_lock_held_(Prcb->PrcbLock)
VOID
FASTCALL
KiEevdfArrive(
    _Inout_ PKPRCB Prcb,
    _Inout_ PKTHREAD Thread);

_Requires_lock_held_(Prcb->PrcbLock)
BOOLEAN
FASTCALL
KiEevdfPreempts(
    _Inout_ PKPRCB Prcb,
    _Inout_ PKTHREAD Thread,
    _Inout_ PKTHREAD Incumbent);

_Requires_lock_held_(Prcb->PrcbLock)
VOID
FASTCALL
KiEevdfBlock(
    _Inout_ PKPRCB Prcb,
    _Inout_ PKTHREAD Thread);

_Requires_lock_held_(Source->PrcbLock)
_Requires_lock_held_(Target->PrcbLock)
VOID
FASTCALL
KiEevdfMigrate(
    _Inout_ PKPRCB Source,
    _Inout_ PKPRCB Target,
    _Inout_ PKTHREAD Thread);

_Requires_lock_held_(Prcb->PrcbLock)
VOID
FASTCALL
KiEevdfPriorityChanged(
    _Inout_ PKPRCB Prcb,
    _Inout_ PKTHREAD Thread);

_Requires_lock_held_(Prcb->PrcbLock)
VOID
FASTCALL
KiEevdfYield(
    _Inout_ PKPRCB Prcb,
    _Inout_ PKTHREAD Thread);

BOOLEAN
FASTCALL
KiEevdfClockTick(
    _Inout_ PKPRCB Prcb,
    _Inout_ PKTHREAD Thread,
    _Out_ PBOOLEAN SliceEnded);

_Requires_lock_not_held_(Prcb->PrcbLock)
BOOLEAN
FASTCALL
KiEevdfTurnOver(
    _Inout_ PKPRCB Prcb,
    _Inout_ PKTHREAD Thread,
    _Out_ PBOOLEAN SliceEnded);

/* EOF */
