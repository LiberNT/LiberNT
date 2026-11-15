#!/usr/bin/env python3
# PROJECT:     LiberNT host-native tests
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     Every video-output child the miniport declares owns a VidPN target slot, whatever their number
# COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
import argparse
import pathlib
import re
import subprocess
import sys
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument('--root', required=True)
parser.add_argument('--cc', required=True)
args = parser.parse_args()
source = (pathlib.Path(args.root) / 'drivers/directx/dxgkrnl/vidpn.c').read_text()


def extract(prefix, name):
    match = re.search(r'(?ms)^' + prefix + r'\n' + name + r'\(.*?^\}\n', source)
    if not match:
        raise RuntimeError(name + ' not found in vidpn.c')
    return match.group(0) + '\n'


fixture = r'''
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef unsigned char UCHAR, BOOLEAN, *PBOOLEAN, KIRQL;
typedef unsigned int ULONG, UINT, *PULONG, D3DDDI_VIDEO_PRESENT_TARGET_ID, D3DKMDT_VIDEO_PRESENT_TARGET_MODE_ID;
typedef int LONG, NTSTATUS;
typedef long long LONG64;
typedef unsigned long long ULONG64, SIZE_T;
typedef void VOID, *PVOID;
#define TRUE 1
#define FALSE 0
#define MAXULONG 0xFFFFFFFFu
#define _In_
#define _Out_
#define _Inout_
#define STATUS_SUCCESS ((NTSTATUS)0)
#define STATUS_INSUFFICIENT_RESOURCES ((NTSTATUS)0xC000009A)
#define STATUS_DEVICE_NOT_READY ((NTSTATUS)0xC00000A3)
#define STATUS_RETRY ((NTSTATUS)0xC000022D)
#define NT_SUCCESS(s) ((NTSTATUS)(s) >= 0)
#define RTL_NUMBER_OF(a) (sizeof(a) / sizeof((a)[0]))
#define CONTAINING_RECORD(a, t, f) ((t *)((char *)(a) - __builtin_offsetof(t, f)))
#define RtlZeroMemory(p, n) memset((p), 0, (n))
#define PAGED_CODE()
#define NonPagedPool 0
#define TAG_DXGK_VIDPN 1
#define TAG_DXGK_MODESET 2
#define DXGKP_VIDPN_SIGNATURE 0x4E704456
#define DXGKP_RUNTIME_TARGET_SLOTS 16
#define Executive 0
#define KernelMode 0

static int Failures, AssertFailures, Warnings, MutexDepth, SpinDepth, FailNextAllocation;
static void Check(int Condition, const char *What)
{
    if (!Condition)
    {
        printf("FAIL: %s\n", What);
        Failures++;
    }
}
#define ASSERT(e) ((e) ? (void)0 : (void)(printf("ASSERT: %s\n", #e), AssertFailures++))
#define DXGKRNL_WARN(...) (Warnings++)
#define DXGKRNL_TRACE(...) ((void)0)

typedef struct _LIST_ENTRY { struct _LIST_ENTRY *Flink, *Blink; } LIST_ENTRY, *PLIST_ENTRY;
static void InitializeListHead(PLIST_ENTRY Head) { Head->Flink = Head->Blink = Head; }
static void InsertTailList(PLIST_ENTRY Head, PLIST_ENTRY Entry)
{
    Entry->Flink = Head;
    Entry->Blink = Head->Blink;
    Head->Blink->Flink = Entry;
    Head->Blink = Entry;
}

static PVOID ExAllocatePoolWithTag(int Pool, SIZE_T Size, ULONG Tag)
{
    (void)Pool;
    (void)Tag;
    if (FailNextAllocation)
    {
        FailNextAllocation = 0;
        return NULL;
    }
    return malloc(Size);
}
static void ExFreePoolWithTag(PVOID P, ULONG Tag) { (void)Tag; free(P); }

typedef struct { int Owned; } KMUTEX, KSPIN_LOCK;
static NTSTATUS KeWaitForSingleObject(KMUTEX *M, int R, int Mode, BOOLEAN A, PVOID T)
{
    (void)R; (void)Mode; (void)A; (void)T;
    ASSERT(!M->Owned);
    M->Owned = 1;
    MutexDepth++;
    return STATUS_SUCCESS;
}
static LONG KeReleaseMutex(KMUTEX *M, BOOLEAN Wait)
{
    (void)Wait;
    ASSERT(M->Owned);
    M->Owned = 0;
    MutexDepth--;
    return 0;
}
static void KeAcquireSpinLock(KSPIN_LOCK *L, KIRQL *Irql) { ASSERT(!L->Owned); L->Owned = 1; SpinDepth++; *Irql = 2; }
static void KeReleaseSpinLock(KSPIN_LOCK *L, KIRQL Irql) { (void)Irql; ASSERT(L->Owned); L->Owned = 0; SpinDepth--; }

typedef enum { TypeUninitialized, TypeVideoOutput, TypeOther } DXGK_CHILD_DEVICE_TYPE;
typedef struct { DXGK_CHILD_DEVICE_TYPE ChildDeviceType; ULONG ChildUid; } DXGK_CHILD_DESCRIPTOR;
typedef struct
{
    DXGK_CHILD_DESCRIPTOR Descriptor;
    LIST_ENTRY ListEntry;
    BOOLEAN Present;
    ULONG64 EnumerationEpoch;
} DXGK_CHILD_PDO_EXTENSION, *PDXGK_CHILD_PDO_EXTENSION;

typedef struct _DXGKRNL_ADAPTER
{
    ULONG NumberOfChildren;
    KMUTEX VidPnMutex;
    KSPIN_LOCK ChildListLock;
    LIST_ENTRY ChildListHead;
    volatile LONG64 ChildEnumerationEpoch;
    volatile LONG ChildRelationsEnumerated;
} DXGKRNL_ADAPTER, *PDXGKRNL_ADAPTER;

static NTSTATUS DxgkHotPlugWorkCoreValidateEnumerationLocked(volatile const LONG64 *Epoch, volatile const LONG *Enumerated, LONG64 Expected)
{
    if (*Epoch != Expected)
        return STATUS_RETRY;
    if (*Enumerated == 0)
        return STATUS_DEVICE_NOT_READY;
    return STATUS_SUCCESS;
}

struct _DXGKP_VIDPN;
typedef struct
{
    SIZE_T NumModes;
    D3DKMDT_VIDEO_PRESENT_TARGET_MODE_ID PinnedModeId;
    struct _DXGKP_VIDPN *Owner;
    D3DDDI_VIDEO_PRESENT_TARGET_ID TargetId;
    UINT NextModeId;
    LONG References;
} DXGKP_VIDPN_TARGET_MODESET, *PDXGKP_VIDPN_TARGET_MODESET;
typedef struct
{
    SIZE_T NumModes;
    UINT NextModeId;
    D3DDDI_VIDEO_PRESENT_TARGET_ID TargetId;
    struct _DXGKP_VIDPN *Owner;
} DXGKP_MONITOR_SOURCE_MODESET, *PDXGKP_MONITOR_SOURCE_MODESET;
typedef struct _DXGKP_VIDPN
{
    ULONG Signature;
    volatile LONG RefCount;
    struct _DXGKRNL_ADAPTER *Adapter;
    ULONG NumSources;
    ULONG NumTargets;
    ULONG TargetCapacity;
    PDXGKP_VIDPN_TARGET_MODESET *TargetModeSets;
    PDXGKP_MONITOR_SOURCE_MODESET *MonitorModeSets;
    LIST_ENTRY PendingObjects;
} DXGKP_VIDPN, *PDXGKP_VIDPN;
'''

harness = r'''
static DXGK_CHILD_PDO_EXTENSION Children[64];
static ULONG ChildCount;

static void AddChild(DXGKRNL_ADAPTER *Adapter, ULONG Uid, DXGK_CHILD_DEVICE_TYPE Type)
{
    PDXGK_CHILD_PDO_EXTENSION Child = &Children[ChildCount++];

    Child->Descriptor.ChildDeviceType = Type;
    Child->Descriptor.ChildUid = Uid;
    Child->Present = TRUE;
    Child->EnumerationEpoch = (ULONG64)Adapter->ChildEnumerationEpoch;
    InsertTailList(&Adapter->ChildListHead, &Child->ListEntry);
}

static PDXGKP_VIDPN CreateVidPn(DXGKRNL_ADAPTER *Adapter)
{
    ULONG NumTargets = 0, Capacity = 0, i;
    PDXGKP_VIDPN VidPn;

    if (!NT_SUCCESS(DxgkpVidPnTargetLayout(Adapter->NumberOfChildren, &NumTargets, &Capacity)))
        return NULL;
    VidPn = DxgkpAllocateVidPn(Adapter, Capacity);
    if (VidPn == NULL)
        return NULL;
    VidPn->NumTargets = NumTargets;
    for (i = 0; i < NumTargets; i++)
    {
        VidPn->TargetModeSets[i] = DxgkpAllocateTargetModeSet(VidPn, i);
        VidPn->MonitorModeSets[i] = DxgkpAllocateMonitorModeSet(VidPn, i);
    }
    return VidPn;
}

static void DestroyVidPn(PDXGKP_VIDPN VidPn)
{
    ULONG i;

    for (i = 0; i < VidPn->TargetCapacity; i++)
    {
        free(VidPn->TargetModeSets[i]);
        free(VidPn->MonitorModeSets[i]);
    }
    free(VidPn);
}

static int AllResolve(PDXGKP_VIDPN VidPn, ULONG FirstUid, ULONG Count)
{
    ULONG i, j, Index[64];

    for (i = 0; i < Count; i++)
    {
        Index[i] = DxgkVidPnTargetIndexFromId(VidPn, FirstUid + i);
        if (Index[i] == MAXULONG || Index[i] >= VidPn->NumTargets)
            return 0;
        if (VidPn->MonitorModeSets[Index[i]]->TargetId != FirstUid + i)
            return 0;
        for (j = 0; j < i; j++)
        {
            if (Index[j] == Index[i])
                return 0;
        }
    }
    return 1;
}

int main(void)
{
    DXGKRNL_ADAPTER Adapter;
    PDXGKP_VIDPN VidPn, Clone;
    ULONG NumTargets = 0, Capacity = 0, Before[24], i;

    Check(DxgkpVidPnTargetLayout(0, &NumTargets, &Capacity) == STATUS_SUCCESS && NumTargets == 1 && Capacity == 17,
          "an adapter that declares no child still gets one target");
    Check(DxgkpVidPnTargetLayout(24, &NumTargets, &Capacity) == STATUS_SUCCESS && NumTargets == 24 && Capacity == 40,
          "24 declared children give 24 targets and run-time room");
    Check(DxgkpVidPnTargetLayout(MAXULONG, &NumTargets, &Capacity) == STATUS_INSUFFICIENT_RESOURCES,
          "a child count whose slots cannot be allocated is refused");

    memset(&Adapter, 0, sizeof(Adapter));
    InitializeListHead(&Adapter.ChildListHead);
    Adapter.ChildEnumerationEpoch = 5;
    Adapter.ChildRelationsEnumerated = 1;
    Adapter.NumberOfChildren = 26;
    for (i = 0; i < 24; i++)
        AddChild(&Adapter, 256 + i, TypeVideoOutput);
    AddChild(&Adapter, 7, TypeOther);
    AddChild(&Adapter, 8, TypeOther);

    VidPn = CreateVidPn(&Adapter);
    Check(VidPn != NULL && VidPn->NumTargets == 26 && VidPn->TargetCapacity == 42, "the VidPN has a slot per declared child");
    if (VidPn == NULL)
        return 1;
    Check((void *)VidPn->TargetModeSets == (void *)(VidPn + 1) &&
          (void *)VidPn->MonitorModeSets == (void *)(VidPn->TargetModeSets + VidPn->TargetCapacity),
          "the slot arrays sit after the VidPN in its allocation");

    DxgkpBindVidPnTargetsToChildren(&Adapter, VidPn);
    Check(AllResolve(VidPn, 256, 24), "all 24 declared outputs, uid 272 included, own a distinct slot");
    Check(Warnings == 0, "no output is reported without a slot");
    Check(DxgkVidPnTargetIndexFromId(VidPn, 7) == MAXULONG, "a non-video child takes no slot");
    for (i = 0; i < 24; i++)
        Before[i] = DxgkVidPnTargetIndexFromId(VidPn, 256 + i);

    DxgkpBindVidPnTargetsToChildren(&Adapter, VidPn);
    for (i = 0; i < 24; i++)
        Check(DxgkVidPnTargetIndexFromId(VidPn, 256 + i) == Before[i], "a rebind keeps every output in its slot");

    for (i = 0; i < 3; i++)
        AddChild(&Adapter, 0x1000 + i, TypeVideoOutput);
    DxgkpBindVidPnTargetsToChildren(&Adapter, VidPn);
    Check(AllResolve(VidPn, 256, 24) && AllResolve(VidPn, 0x1000, 3), "outputs reported at run time take the free placeholder slots");
    Check(VidPn->NumTargets == 27, "the two free placeholder slots are used before one new slot");

    for (i = 3; i < 25; i++)
        AddChild(&Adapter, 0x1000 + i, TypeVideoOutput);
    DxgkpBindVidPnTargetsToChildren(&Adapter, VidPn);
    Check(VidPn->NumTargets == VidPn->TargetCapacity, "run-time outputs grow the VidPN up to its capacity");
    Check(AllResolve(VidPn, 256, 24) && AllResolve(VidPn, 0x1000, 18), "every output that fits owns a distinct slot");
    Check(DxgkVidPnTargetIndexFromId(VidPn, 0x1000 + 18) == MAXULONG, "an output beyond the capacity stays unreachable");
    Check(Warnings == 1, "the output beyond the capacity is reported once");

    Clone = DxgkpAllocateVidPn(&Adapter, VidPn->TargetCapacity);
    Check(Clone != NULL && Clone->TargetCapacity == VidPn->TargetCapacity && Clone->RefCount == 1 &&
          Clone->Signature == DXGKP_VIDPN_SIGNATURE && Clone->PendingObjects.Flink == &Clone->PendingObjects,
          "a VidPN allocation is initialized with its capacity");
    if (Clone != NULL)
    {
        for (i = 0; i < Clone->TargetCapacity; i++)
            Check(Clone->TargetModeSets[i] == NULL && Clone->MonitorModeSets[i] == NULL, "a fresh VidPN has empty slots");
        free(Clone);
    }

    Adapter.ChildRelationsEnumerated = 0;
    Warnings = 0;
    AddChild(&Adapter, 0x2000, TypeVideoOutput);
    DxgkpBindVidPnTargetsToChildren(&Adapter, VidPn);
    Check(DxgkVidPnTargetIndexFromId(VidPn, 0x2000) == MAXULONG && Warnings == 0, "nothing is bound while children are being enumerated");

    Adapter.ChildRelationsEnumerated = 1;
    FailNextAllocation = 1;
    DxgkpBindVidPnTargetsToChildren(&Adapter, VidPn);
    Check(Warnings == 1, "a failed scratch allocation is reported and the binding skipped");

    Check(MutexDepth == 0 && SpinDepth == 0, "every path releases the VidPN mutex and the child list lock");
    Check(AssertFailures == 0, "no assertion fired");
    DestroyVidPn(VidPn);
    printf("%s: %d failure(s)\n", Failures ? "FAIL" : "PASS", Failures);
    return Failures != 0;
}
'''

program = fixture
program += extract('ULONG', 'DxgkVidPnTargetIndexFromId')
program += extract('static PDXGKP_VIDPN_TARGET_MODESET', 'DxgkpAllocateTargetModeSet')
program += extract('static PDXGKP_MONITOR_SOURCE_MODESET', 'DxgkpAllocateMonitorModeSet')
program += extract('static NTSTATUS', 'DxgkpVidPnTargetLayout')
program += extract('static PDXGKP_VIDPN', 'DxgkpAllocateVidPn')
program += extract('static VOID', 'DxgkpBindVidPnTargetsToChildren')
program += harness
with tempfile.TemporaryDirectory() as temp:
    path = pathlib.Path(temp) / 'test.c'
    binary = pathlib.Path(temp) / 'test'
    path.write_text(program)
    subprocess.run([args.cc, '-std=gnu11', '-Wall', '-Wno-unused-function', '-fsanitize=address,undefined', '-o', str(binary), str(path)], check=True)
    sys.exit(subprocess.run([str(binary)]).returncode)
