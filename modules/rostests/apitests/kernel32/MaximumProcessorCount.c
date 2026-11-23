/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test that the maximum processor count matches the processors the system has
 * COPYRIGHT:   Copyright 2026 mikuta0407 <mikuta0407@mikuta0407.net>
 */

#include "precomp.h"

START_TEST(MaximumProcessorCount)
{
    SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX *Info;
    DWORD Size = 0, Active = GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
    WORD Index;

    ok(Active != 0, "GetActiveProcessorCount failed with %lu\n", GetLastError());
    ok(GetMaximumProcessorCount(ALL_PROCESSOR_GROUPS) == Active, "Maximum %lu, active %lu\n",
       GetMaximumProcessorCount(ALL_PROCESSOR_GROUPS), Active);
    ok(GetMaximumProcessorCount(0) == GetActiveProcessorCount(0), "Group 0: maximum %lu, active %lu\n",
       GetMaximumProcessorCount(0), GetActiveProcessorCount(0));

    ok(!GetLogicalProcessorInformationEx(RelationGroup, NULL, &Size), "No error\n");
    ok(GetLastError() == ERROR_INSUFFICIENT_BUFFER, "Error %lu\n", GetLastError());
    Info = HeapAlloc(GetProcessHeap(), 0, Size);
    if (!Info)
        return;
    ok(GetLogicalProcessorInformationEx(RelationGroup, Info, &Size), "Failed with %lu\n", GetLastError());
    ok(Info->Relationship == RelationGroup, "Relationship %d\n", Info->Relationship);
    for (Index = 0; Index < Info->Group.ActiveGroupCount; Index++)
    {
        ok(Info->Group.GroupInfo[Index].MaximumProcessorCount == Info->Group.GroupInfo[Index].ActiveProcessorCount,
           "Group %u: maximum %u, active %u\n", Index, Info->Group.GroupInfo[Index].MaximumProcessorCount,
           Info->Group.GroupInfo[Index].ActiveProcessorCount);
    }
    HeapFree(GetProcessHeap(), 0, Info);
}
