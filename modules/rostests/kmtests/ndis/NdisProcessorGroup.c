/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     NDIS processor group query tests
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>
#include <ndis.h>
#include "NdisTestExport.h"

typedef ULONG (NTAPI *PFN_NDIS_SYSTEM_ACTIVE_PROCESSOR_COUNT)(PKAFFINITY);

__declspec(dllimport) USHORT NTAPI NdisActiveGroupCount(VOID);
__declspec(dllimport) USHORT NTAPI NdisMaxGroupCount(VOID);
__declspec(dllimport) ULONG NTAPI NdisGroupMaxProcessorCount(USHORT);
__declspec(dllimport) ULONG NTAPI NdisGroupActiveProcessorCount(USHORT);
__declspec(dllimport) KAFFINITY NTAPI NdisGroupActiveProcessorMask(USHORT);
__declspec(dllimport) PROCESSOR_NUMBER NTAPI NdisCurrentGroupAndProcessor(VOID);
__declspec(dllimport) ULONG NTAPI NdisProcessorNumberToIndex(PROCESSOR_NUMBER);
__declspec(dllimport) NTSTATUS NTAPI NdisProcessorIndexToNumber(ULONG, PPROCESSOR_NUMBER);

START_TEST(NdisProcessorGroup)
{
    PFN_NDIS_SYSTEM_ACTIVE_PROCESSOR_COUNT pSystemActiveProcessorCount = NdisTestFindExport("NdisSystemActiveProcessorCount");
    KAFFINITY NdisAffinity = 0, KeAffinity = 0;
    PROCESSOR_NUMBER Number, KeNumber;
    USHORT Group, Groups;
    ULONG Index, Count;
    KIRQL OldIrql;
    NTSTATUS Status;

    if (pSystemActiveProcessorCount)
    {
        ok_eq_ulong(pSystemActiveProcessorCount(&NdisAffinity), KeQueryActiveProcessorCount(&KeAffinity));
        ok_eq_ulongptr(NdisAffinity, KeAffinity);
    }
    else
    {
        trace("ndis.sys does not export NdisSystemActiveProcessorCount\n");
    }

    Groups = NdisActiveGroupCount();
    ok_eq_uint(Groups, KeQueryActiveGroupCount());
    ok_eq_uint(NdisMaxGroupCount(), KeQueryMaximumGroupCount());
    ok(Groups >= 1, "Groups = %u\n", Groups);

    Count = 0;
    for (Group = 0; Group < Groups; Group++)
    {
        ok_eq_ulong(NdisGroupMaxProcessorCount(Group), KeQueryMaximumProcessorCountEx(Group));
        ok_eq_ulong(NdisGroupActiveProcessorCount(Group), KeQueryActiveProcessorCountEx(Group));
        ok_eq_ulongptr(NdisGroupActiveProcessorMask(Group), KeQueryGroupAffinity(Group));
        Count += NdisGroupActiveProcessorCount(Group);
    }
    ok_eq_ulong(Count, KeQueryActiveProcessorCountEx(ALL_PROCESSOR_GROUPS));

    KeRaiseIrql(DISPATCH_LEVEL, &OldIrql);
    Number = NdisCurrentGroupAndProcessor();
    KeGetCurrentProcessorNumberEx(&KeNumber);
    KeLowerIrql(OldIrql);
    ok_eq_uint(Number.Group, KeNumber.Group);
    ok_eq_uint(Number.Number, KeNumber.Number);
    ok_eq_uint(Number.Reserved, 0);

    for (Index = 0; Index < Count; Index++)
    {
        RtlFillMemory(&Number, sizeof(Number), 0xCC);
        Status = NdisProcessorIndexToNumber(Index, &Number);
        ok_eq_hex(Status, STATUS_SUCCESS);
        ok_eq_ulong(NdisProcessorNumberToIndex(Number), Index);
        ok_eq_ulong(NdisProcessorNumberToIndex(Number), KeGetProcessorIndexFromNumber(&Number));
    }

    Status = NdisProcessorIndexToNumber(Count, &Number);
    ok_eq_hex(Status, STATUS_INVALID_PARAMETER);
}
