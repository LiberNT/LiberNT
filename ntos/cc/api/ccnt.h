/*
 * PROJECT:     LiberNT Cache Manager
 * FILE:        ntoskrnl/cc/api/ccnt.h
 * PURPOSE:     Internal NT cache manager interfaces
 *
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF
 * SPDX-License-Identifier: GPL-3.0-only
 */

#pragma once

#ifdef CC_HOST_TEST
#include <mmcc/cc/host/ccntshim.h>
#else
#include <ntoskrnl.h>
#include <mm/nt/mint.h>
#endif
#include "../include/ccengine.h"
#include <mm/include/miasync.h>

#define CC_NT_MAP_TAG      'pMcC'
#define CC_NT_PRIVATE_TAG  'vPcC'

typedef struct _CC_NT_MAP
{
    CC_MAP Map;
    LIST_ENTRY Link;
    PFILE_OBJECT FileObject;
    PSECTION_OBJECT_POINTERS Pointers;
    PMI_CONTROL_AREA Control;
    CACHE_MANAGER_CALLBACKS Callbacks;
    PVOID LazyWriteContext;
    volatile LONG ReferenceCount;
    BOOLEAN PinAccess;
    BOOLEAN Closing;
    BOOLEAN PurgeOnClose;
    LARGE_INTEGER AllocationSize;
    PVOID LogHandle;
    PFLUSH_TO_LSN FlushToLsn;
    ULONG Flags;
    KEVENT *UninitializeEvent;
    MI_ASYNC_DRAIN ReadDrain;
    KEVENT LazyWriteGate;
} CC_NT_MAP, *PCC_NT_MAP;

C_ASSERT(FIELD_OFFSET(CC_NT_MAP, Map) == 0);
C_ASSERT(sizeof(CC_NT_MAP) <= 0x7FFF);

typedef struct _CC_NT_PRIVATE
{
    CC_READ_AHEAD ReadAhead;
    PFILE_OBJECT FileObject;
} CC_NT_PRIVATE, *PCC_NT_PRIVATE;

typedef struct _CC_NT_BCB
{
    union
    {
        CC_BCB Engine;
        PUBLIC_BCB Public;
    };
} CC_NT_BCB, *PCC_NT_BCB;

C_ASSERT(FIELD_OFFSET(CC_NT_BCB, Engine) == 0);
C_ASSERT(FIELD_OFFSET(CC_NT_BCB, Public) == 0);
C_ASSERT(FIELD_OFFSET(PUBLIC_BCB, NodeTypeCode) == FIELD_OFFSET(CC_BCB, NodeTypeCode));
C_ASSERT(FIELD_OFFSET(PUBLIC_BCB, NodeByteSize) == FIELD_OFFSET(CC_BCB, Dirty));
C_ASSERT(FIELD_OFFSET(PUBLIC_BCB, MappedLength) == FIELD_OFFSET(CC_BCB, Length));
C_ASSERT(FIELD_OFFSET(PUBLIC_BCB, MappedFileOffset) == FIELD_OFFSET(CC_BCB, FileOffset));

#define CC_NT_BCB_FROM_PUBLIC(p) CONTAINING_RECORD((p), CC_NT_BCB, Public)

extern CC_CACHE CcNtCache;
extern CC_BACKING_OPS CcNtBackingOps;
#define CC_NT_MAP_BUCKETS 64

typedef struct _CC_NT_MAP_BUCKET
{
    KSPIN_LOCK Lock;
    LIST_ENTRY List;
} CC_NT_MAP_BUCKET, *PCC_NT_MAP_BUCKET;

extern CC_NT_MAP_BUCKET CcNtMapBuckets[CC_NT_MAP_BUCKETS];

VOID CcNtInitializeMapTable(VOID);
PCC_NT_MAP_BUCKET CcNtMapBucket(_In_ PSECTION_OBJECT_POINTERS Pointers);
PCC_NT_MAP CcNtInsertMap(_Inout_ PCC_NT_MAP Fresh);
VOID CcNtLinkMap(_Inout_ PCC_NT_MAP NtMap);
BOOLEAN CcNtIsMapTableEmpty(VOID);
VOID CcNtMapBarrier(_In_ PCC_NT_MAP NtMap);
VOID CcNtExpireClosedMaps(VOID);

PCC_NT_MAP CcNtReferenceMap(_In_ PSECTION_OBJECT_POINTERS Pointers);
PCC_NT_MAP CcNtReferenceFileMap(_In_ PFILE_OBJECT FileObject);
VOID CcNtDereferenceMap(_Inout_ PCC_NT_MAP NtMap);
VOID CcNtDestroyMap(_Inout_ PCC_NT_MAP NtMap);
VOID CcNtBcbCreated(_Inout_ PCC_BCB Bcb);
VOID CcNtBcbDeleting(_Inout_ PCC_BCB Bcb);
NTSTATUS CcNtFlushMap(_Inout_ PCC_NT_MAP NtMap, _In_ ULONG64 Offset, _In_ ULONG64 Length, _In_ ULONG MaximumPages,
                      _Out_opt_ PULONG PagesFlushed);
VOID CcNtKickLazyWriter(VOID);
VOID CcNtProcessDeferredWrites(VOID);
