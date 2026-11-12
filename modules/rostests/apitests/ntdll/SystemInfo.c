/*
 * PROJECT:         ReactOS api tests
 * LICENSE:         GPL - See COPYING in the top level directory
 * PURPOSE:         Test for NtQuery/SetSystemInformation
 * PROGRAMMERS:     Timo Kreuzer
 *                  Thomas Faber <thomas.faber@reactos.org>
 */

#include "precomp.h"
#include <versionhelpers.h>

#define ntv6(x) (LOBYTE(LOWORD(GetVersion())) >= 6 ? (x) : 0)

static
void
Test_Flags(void)
{
    NTSTATUS Status;
    BOOLEAN WasEnabled;
    ULONG ReturnLength;
    ULONG Flags;
    ULONG Buffer[2];
    ULONG Buffer2[2];
    PSYSTEM_FLAGS_INFORMATION FlagsInfo = (PVOID)Buffer;
    BOOLEAN PrivilegeEnabled;

    /* Make sure we don't have debug privileges initially, otherwise WHS testbot fails */
    Status = RtlAdjustPrivilege(SE_SYSTEMTIME_PRIVILEGE, FALSE, FALSE, &PrivilegeEnabled);
    ok(Status == STATUS_SUCCESS, "RtlAdjustPrivilege returned %lx\n", Status);

    /* Query */
    ReturnLength = 0x55555555;
    Status = NtQuerySystemInformation(SystemFlagsInformation, NULL, 0, &ReturnLength);
    ok(Status == STATUS_INFO_LENGTH_MISMATCH, "NtQuerySystemInformation returned %lx\n", Status);
    ok(ReturnLength == 0 ||
       ntv6(ReturnLength == sizeof(SYSTEM_FLAGS_INFORMATION)), "ReturnLength = %lu\n", ReturnLength);

    ReturnLength = 0x55555555;
    Status = NtQuerySystemInformation(SystemFlagsInformation, NULL, sizeof(SYSTEM_FLAGS_INFORMATION) - 1, &ReturnLength);
    ok(Status == STATUS_ACCESS_VIOLATION, "NtQuerySystemInformation returned %lx\n", Status);
    ok(ReturnLength == 0x55555555, "ReturnLength = %lu\n", ReturnLength);

    ReturnLength = 0x55555555;
    Status = NtQuerySystemInformation(SystemFlagsInformation, (PVOID)2, 0, &ReturnLength);
    ok(Status == STATUS_INFO_LENGTH_MISMATCH, "NtQuerySystemInformation returned %lx\n", Status);
    ok(ReturnLength == 0 ||
       ntv6(ReturnLength == sizeof(SYSTEM_FLAGS_INFORMATION)), "ReturnLength = %lu\n", ReturnLength);

    ReturnLength = 0x55555555;
    Status = NtQuerySystemInformation(SystemFlagsInformation, (PVOID)4, 0, &ReturnLength);
    ok(Status == STATUS_INFO_LENGTH_MISMATCH, "NtQuerySystemInformation returned %lx\n", Status);
    ok(ReturnLength == 0 ||
       ntv6(ReturnLength == sizeof(SYSTEM_FLAGS_INFORMATION)), "ReturnLength = %lu\n", ReturnLength);

    ReturnLength = 0x55555555;
    RtlFillMemory(Buffer, sizeof(Buffer), 0x55);
    Status = NtQuerySystemInformation(SystemFlagsInformation, (PUCHAR)FlagsInfo + 2, sizeof(SYSTEM_FLAGS_INFORMATION), &ReturnLength);
    ok(Status == STATUS_DATATYPE_MISALIGNMENT, "NtQuerySystemInformation returned %lx\n", Status);
    ok(ReturnLength == 0x55555555, "ReturnLength = %lu\n", ReturnLength);
    ok(Buffer[0] == 0x55555555, "Buffer[0] = %lx\n", Buffer[0]);
    ok(Buffer[1] == 0x55555555, "Buffer[1] = %lx\n", Buffer[1]);

    ReturnLength = 0x55555555;
    RtlFillMemory(Buffer, sizeof(Buffer), 0x55);
    Status = NtQuerySystemInformation(SystemFlagsInformation, (PUCHAR)FlagsInfo + 2, sizeof(SYSTEM_FLAGS_INFORMATION) - 1, &ReturnLength);
    ok(Status == STATUS_DATATYPE_MISALIGNMENT, "NtQuerySystemInformation returned %lx\n", Status);
    ok(ReturnLength == 0x55555555, "ReturnLength = %lu\n", ReturnLength);
    ok(Buffer[0] == 0x55555555, "Buffer[0] = %lx\n", Buffer[0]);
    ok(Buffer[1] == 0x55555555, "Buffer[1] = %lx\n", Buffer[1]);

    ReturnLength = 0x55555555;
    RtlFillMemory(Buffer, sizeof(Buffer), 0x55);
    Status = NtQuerySystemInformation(SystemFlagsInformation, FlagsInfo, sizeof(SYSTEM_FLAGS_INFORMATION) - 1, &ReturnLength);
    ok(Status == STATUS_INFO_LENGTH_MISMATCH, "NtQuerySystemInformation returned %lx\n", Status);
    ok(ReturnLength == 0 ||
       ntv6(ReturnLength == sizeof(SYSTEM_FLAGS_INFORMATION)), "ReturnLength = %lu\n", ReturnLength);
    ok(Buffer[0] == 0x55555555, "Buffer[0] = %lx\n", Buffer[0]);
    ok(Buffer[1] == 0x55555555, "Buffer[1] = %lx\n", Buffer[1]);

    ReturnLength = 0x55555555;
    RtlFillMemory(Buffer, sizeof(Buffer), 0x55);
    Status = NtQuerySystemInformation(SystemFlagsInformation, FlagsInfo, sizeof(SYSTEM_FLAGS_INFORMATION), &ReturnLength);
    ok(Status == STATUS_SUCCESS, "NtQuerySystemInformation returned %lx\n", Status);
    ok(ReturnLength == sizeof(SYSTEM_FLAGS_INFORMATION), "ReturnLength = %lu\n", ReturnLength);
    ok(FlagsInfo->Flags != 0x55555555, "Flags = %lx\n", FlagsInfo->Flags);
    ok(Buffer[1] == 0x55555555, "Buffer[1] = %lx\n", Buffer[1]);
    Flags = FlagsInfo->Flags;

    ReturnLength = 0x55555555;
    RtlFillMemory(Buffer, sizeof(Buffer), 0x55);
    Status = NtQuerySystemInformation(SystemFlagsInformation, FlagsInfo, sizeof(SYSTEM_FLAGS_INFORMATION) + 1, &ReturnLength);
    ok(Status == STATUS_INFO_LENGTH_MISMATCH, "NtQuerySystemInformation returned %lx\n", Status);
    ok(ReturnLength == 0 ||
       ntv6(ReturnLength == sizeof(SYSTEM_FLAGS_INFORMATION)), "ReturnLength = %lu\n", ReturnLength);
    ok(Buffer[0] == 0x55555555, "Buffer[0] = %lx\n", Buffer[0]);
    ok(Buffer[1] == 0x55555555, "Buffer[1] = %lx\n", Buffer[1]);

    RtlFillMemory(Buffer, sizeof(Buffer), 0x55);
    Status = NtQuerySystemInformation(SystemFlagsInformation, FlagsInfo, sizeof(SYSTEM_FLAGS_INFORMATION), NULL);
    ok(Status == STATUS_SUCCESS, "NtQuerySystemInformation returned %lx\n", Status);
    ok(FlagsInfo->Flags == Flags, "Flags = %lx, expected %lx\n", FlagsInfo->Flags, Flags);
    ok(Buffer[1] == 0x55555555, "Buffer[1] = %lx\n", Buffer[1]);

    RtlFillMemory(Buffer, sizeof(Buffer), 0x55);
    Status = NtQuerySystemInformation(SystemFlagsInformation, FlagsInfo, sizeof(SYSTEM_FLAGS_INFORMATION), (PVOID)4);
    ok(Status == STATUS_ACCESS_VIOLATION, "NtQuerySystemInformation returned %lx\n", Status);
    ok(Buffer[0] == 0x55555555, "Buffer[0] = %lx\n", Buffer[0]);
    ok(Buffer[1] == 0x55555555, "Buffer[1] = %lx\n", Buffer[1]);

    RtlFillMemory(Buffer, sizeof(Buffer), 0x55);
    Status = NtQuerySystemInformation(SystemFlagsInformation, FlagsInfo, sizeof(SYSTEM_FLAGS_INFORMATION), (PVOID)2);
    ok(Status == STATUS_ACCESS_VIOLATION, "NtQuerySystemInformation returned %lx\n", Status);
    ok(Buffer[0] == 0x55555555, "Buffer[0] = %lx\n", Buffer[0]);
    ok(Buffer[1] == 0x55555555, "Buffer[1] = %lx\n", Buffer[1]);

    RtlFillMemory(Buffer, sizeof(Buffer), 0x55);
    Status = NtQuerySystemInformation(SystemFlagsInformation, FlagsInfo, sizeof(SYSTEM_FLAGS_INFORMATION), (PVOID)1);
    ok(Status == STATUS_ACCESS_VIOLATION, "NtQuerySystemInformation returned %lx\n", Status);
    ok(Buffer[0] == 0x55555555, "Buffer[0] = %lx\n", Buffer[0]);
    ok(Buffer[1] == 0x55555555, "Buffer[1] = %lx\n", Buffer[1]);

    RtlFillMemory(Buffer2, sizeof(Buffer2), 0x55);
    RtlFillMemory(Buffer, sizeof(Buffer), 0x55);
    Status = NtQuerySystemInformation(SystemFlagsInformation, FlagsInfo, sizeof(SYSTEM_FLAGS_INFORMATION), (PULONG)((PUCHAR)Buffer2 + 1));
    ok(Status == STATUS_SUCCESS, "NtQuerySystemInformation returned %lx\n", Status);
    ok(FlagsInfo->Flags == Flags, "Flags = %lx, expected %lx\n", FlagsInfo->Flags, Flags);
    ok(Buffer[1] == 0x55555555, "Buffer[1] = %lx\n", Buffer[1]);
    ok(Buffer2[0] == 0x00000455, "Buffer2[0] = %lx\n", Buffer2[0]);
    ok(Buffer2[1] == 0x55555500, "Buffer2[1] = %lx\n", Buffer2[1]);

    /* Set */
    Status = NtSetSystemInformation(SystemFlagsInformation, NULL, 0);
    ok(Status == STATUS_INFO_LENGTH_MISMATCH, "NtSetSystemInformation returned %lx\n", Status);

    Status = NtSetSystemInformation(SystemFlagsInformation, NULL, sizeof(SYSTEM_FLAGS_INFORMATION) - 1);
    ok(Status == STATUS_INFO_LENGTH_MISMATCH, "NtSetSystemInformation returned %lx\n", Status);

    RtlAdjustPrivilege(SE_DEBUG_PRIVILEGE, FALSE, FALSE, &WasEnabled);
    Status = NtSetSystemInformation(SystemFlagsInformation, NULL, sizeof(SYSTEM_FLAGS_INFORMATION));
    RtlAdjustPrivilege(SE_DEBUG_PRIVILEGE, WasEnabled, FALSE, &WasEnabled);
    ok(Status == (IsWindows7OrGreater() ? STATUS_ACCESS_DENIED : STATUS_ACCESS_VIOLATION), "NtSetSystemInformation returned %lx\n", Status);

    Status = NtSetSystemInformation(SystemFlagsInformation, (PVOID)2, sizeof(SYSTEM_FLAGS_INFORMATION));
    ok(Status == STATUS_DATATYPE_MISALIGNMENT, "NtSetSystemInformation returned %lx\n", Status);

    Status = RtlAdjustPrivilege(SE_DEBUG_PRIVILEGE, TRUE, FALSE, &PrivilegeEnabled);
    if (!NT_SUCCESS(Status))
    {
        skip("Cannot acquire SeDebugPrivilege\n");
        return;
    }

    Status = NtSetSystemInformation(SystemFlagsInformation, NULL, sizeof(SYSTEM_FLAGS_INFORMATION));
    ok(Status == STATUS_ACCESS_VIOLATION, "NtSetSystemInformation returned %lx\n", Status);

    Status = NtSetSystemInformation(SystemFlagsInformation, (PVOID)2, sizeof(SYSTEM_FLAGS_INFORMATION));
    ok(Status == STATUS_DATATYPE_MISALIGNMENT, "NtSetSystemInformation returned %lx\n", Status);

    Status = NtSetSystemInformation(SystemFlagsInformation, (PVOID)4, sizeof(SYSTEM_FLAGS_INFORMATION));
    ok(Status == STATUS_ACCESS_VIOLATION, "NtSetSystemInformation returned %lx\n", Status);

    FlagsInfo->Flags = Flags;
    Status = NtSetSystemInformation(SystemFlagsInformation, (PUCHAR)FlagsInfo + 2, sizeof(SYSTEM_FLAGS_INFORMATION));
    ok(Status == STATUS_DATATYPE_MISALIGNMENT, "NtSetSystemInformation returned %lx\n", Status);

    Status = NtSetSystemInformation(SystemFlagsInformation, (PUCHAR)FlagsInfo + 2, sizeof(SYSTEM_FLAGS_INFORMATION) - 1);
    ok(Status == STATUS_DATATYPE_MISALIGNMENT, "NtSetSystemInformation returned %lx\n", Status);

    Status = NtSetSystemInformation(SystemFlagsInformation, FlagsInfo, sizeof(SYSTEM_FLAGS_INFORMATION) - 1);
    ok(Status == STATUS_INFO_LENGTH_MISMATCH, "NtSetSystemInformation returned %lx\n", Status);

    Status = NtSetSystemInformation(SystemFlagsInformation, FlagsInfo, sizeof(SYSTEM_FLAGS_INFORMATION) + 1);
    ok(Status == STATUS_INFO_LENGTH_MISMATCH, "NtSetSystemInformation returned %lx\n", Status);

    Status = NtSetSystemInformation(SystemFlagsInformation, FlagsInfo, sizeof(SYSTEM_FLAGS_INFORMATION));
    ok(Status == STATUS_SUCCESS, "NtSetSystemInformation returned %lx\n", Status);

    ok(FlagsInfo->Flags == Flags, "Flags = %lx, expected %lu\n", FlagsInfo->Flags, Flags);

    Status = RtlAdjustPrivilege(SE_DEBUG_PRIVILEGE, PrivilegeEnabled, FALSE, &PrivilegeEnabled);
    ok(Status == STATUS_SUCCESS, "RtlAdjustPrivilege returned %lx\n", Status);
}

static
void
Test_TimeAdjustment(void)
{
    SYSTEM_QUERY_TIME_ADJUST_INFORMATION TimeInfoOrg, GetTimeInfo;
    SYSTEM_SET_TIME_ADJUST_INFORMATION SetTimeInfo;
    NTSTATUS Status;
    ULONG ReturnLength;
    BOOLEAN PrivilegeEnabled;

    /* Make sure we don't have debug privileges initially, otherwise WHS testbot fails */
    Status = RtlAdjustPrivilege(SE_SYSTEMTIME_PRIVILEGE, FALSE, FALSE, &PrivilegeEnabled);
    ok(Status == STATUS_SUCCESS, "RtlAdjustPrivilege returned %lx\n", Status);

    SetTimeInfo.TimeAdjustment = 0;
    SetTimeInfo.Enable = 0;

    /* Query original values */
    Status = NtQuerySystemInformation(SystemTimeAdjustmentInformation,
                                      &TimeInfoOrg,
                                      sizeof(TimeInfoOrg),
                                      &ReturnLength);

    /* Test without privilege */
    Status = NtSetSystemInformation(SystemTimeAdjustmentInformation,
                                    &SetTimeInfo,
                                    sizeof(SetTimeInfo));
    ok_ntstatus(Status, STATUS_PRIVILEGE_NOT_HELD);

    /* Get the required privilege */
    Status = RtlAdjustPrivilege(SE_SYSTEMTIME_PRIVILEGE, TRUE, FALSE, &PrivilegeEnabled);
    if (!NT_SUCCESS(Status))
    {
        skip("Cannot acquire SeSystemTimePrivilege\n");
        return;
    }

    /* Test wrong length */
    Status = NtSetSystemInformation(SystemTimeAdjustmentInformation,
                                    &SetTimeInfo,
                                    sizeof(SetTimeInfo) + 1);
    ok_ntstatus(Status, STATUS_INFO_LENGTH_MISMATCH);

    /* Test both members 0 */
    Status = NtSetSystemInformation(SystemTimeAdjustmentInformation,
                                    &SetTimeInfo,
                                    sizeof(SetTimeInfo));
    ok_ntstatus(Status, IsWindows7OrGreater() ? STATUS_INTEGER_DIVIDE_BY_ZERO : STATUS_INVALID_PARAMETER_2);

    /* Set huge value */
    SetTimeInfo.TimeAdjustment = -1;
    SetTimeInfo.Enable = 0;
    Status = NtSetSystemInformation(SystemTimeAdjustmentInformation,
                                    &SetTimeInfo,
                                    sizeof(SetTimeInfo));
    ok_ntstatus(Status, STATUS_SUCCESS);

    /* Query the result */
    Status = NtQuerySystemInformation(SystemTimeAdjustmentInformation,
                                      &GetTimeInfo,
                                      sizeof(GetTimeInfo),
                                      &ReturnLength);
    ok_ntstatus(Status, STATUS_SUCCESS);
    if (GetNTVersion() < _WIN32_WINNT_WIN10)
        ok_long(GetTimeInfo.TimeAdjustment, IsWindows7OrGreater() ? 0x7076cc : -1);
    ok_long(GetTimeInfo.Enable, 0);

    /* set Enable to 1 */
    SetTimeInfo.TimeAdjustment = -1;
    SetTimeInfo.Enable = 1;
    Status = NtSetSystemInformation(SystemTimeAdjustmentInformation,
                                    &SetTimeInfo,
                                    sizeof(SetTimeInfo));
    ok_ntstatus(Status, STATUS_SUCCESS);

    /* Query the result */
    Status = NtQuerySystemInformation(SystemTimeAdjustmentInformation,
                                      &GetTimeInfo,
                                      sizeof(GetTimeInfo),
                                      &ReturnLength);
    ok_ntstatus(Status, STATUS_SUCCESS);
    ok_long(GetTimeInfo.TimeAdjustment, GetTimeInfo.TimeIncrement);
    ok_long(GetTimeInfo.Enable, 1);

    /* An adjustment enabled at the clock period's own value still reads as enabled (GetSystemTimeAdjustment) */
    SetTimeInfo.TimeAdjustment = GetTimeInfo.TimeIncrement;
    SetTimeInfo.Enable = 0;
    Status = NtSetSystemInformation(SystemTimeAdjustmentInformation,
                                    &SetTimeInfo,
                                    sizeof(SetTimeInfo));
    ok_ntstatus(Status, STATUS_SUCCESS);
    Status = NtQuerySystemInformation(SystemTimeAdjustmentInformation,
                                      &GetTimeInfo,
                                      sizeof(GetTimeInfo),
                                      &ReturnLength);
    ok_ntstatus(Status, STATUS_SUCCESS);
    ok_long(GetTimeInfo.TimeAdjustment, GetTimeInfo.TimeIncrement);
    ok_long(GetTimeInfo.Enable, 0);

    /* Restore original values */
    SetTimeInfo.TimeAdjustment = TimeInfoOrg.TimeAdjustment;
    SetTimeInfo.Enable = TimeInfoOrg.Enable;
    Status = NtSetSystemInformation(SystemTimeAdjustmentInformation,
                                    &SetTimeInfo,
                                    sizeof(SetTimeInfo));
    ok_ntstatus(Status, STATUS_SUCCESS);

    Status = RtlAdjustPrivilege(SE_SYSTEMTIME_PRIVILEGE, PrivilegeEnabled, FALSE, &PrivilegeEnabled);
    ok(Status == STATUS_SUCCESS, "RtlAdjustPrivilege returned %lx\n", Status);
}

static
void
Test_KernelDebugger(void)
{
    NTSTATUS Status;
    ULONG ReturnLength;
    ULONG Buffer[2];
    PSYSTEM_KERNEL_DEBUGGER_INFORMATION DebuggerInfo = (PVOID)Buffer;

    /* Query */
    ReturnLength = 0x55555555;
    Status = NtQuerySystemInformation(SystemKernelDebuggerInformation, NULL, 0, &ReturnLength);
    ok(Status == STATUS_INFO_LENGTH_MISMATCH, "NtQuerySystemInformation returned %lx\n", Status);
    ok(ReturnLength == 0 ||
       ntv6(ReturnLength == sizeof(SYSTEM_KERNEL_DEBUGGER_INFORMATION)), "ReturnLength = %lu\n", ReturnLength);

    ReturnLength = 0x55555555;
    RtlFillMemory(Buffer, sizeof(Buffer), 0x55);
    Status = NtQuerySystemInformation(SystemKernelDebuggerInformation, DebuggerInfo, sizeof(SYSTEM_KERNEL_DEBUGGER_INFORMATION) - 1, &ReturnLength);
    ok(Status == STATUS_INFO_LENGTH_MISMATCH, "NtQuerySystemInformation returned %lx\n", Status);
    ok(ReturnLength == 0 ||
       ntv6(ReturnLength == sizeof(SYSTEM_KERNEL_DEBUGGER_INFORMATION)), "ReturnLength = %lu\n", ReturnLength);
    ok(Buffer[0] == 0x55555555, "Buffer[0] = %lx\n", Buffer[0]);
    ok(Buffer[1] == 0x55555555, "Buffer[1] = %lx\n", Buffer[1]);

    ReturnLength = 0x55555555;
    RtlFillMemory(Buffer, sizeof(Buffer), 0x55);
    Status = NtQuerySystemInformation(SystemKernelDebuggerInformation, (PUCHAR)DebuggerInfo + 1, sizeof(SYSTEM_KERNEL_DEBUGGER_INFORMATION), &ReturnLength);
    ok(Status == STATUS_SUCCESS, "NtQuerySystemInformation returned %lx\n", Status);
    ok(ReturnLength == sizeof(SYSTEM_KERNEL_DEBUGGER_INFORMATION), "ReturnLength = %lu\n", ReturnLength);
    ok((Buffer[0] & 0x55fefe55) == 0x55000055, "Buffer[0] = %lx\n", Buffer[0]);
    ok(Buffer[1] == 0x55555555, "Buffer[1] = %lx\n", Buffer[1]);

    ReturnLength = 0x55555555;
    RtlFillMemory(Buffer, sizeof(Buffer), 0x55);
    Status = NtQuerySystemInformation(SystemKernelDebuggerInformation, DebuggerInfo, sizeof(SYSTEM_KERNEL_DEBUGGER_INFORMATION), &ReturnLength);
    ok(Status == STATUS_SUCCESS, "NtQuerySystemInformation returned %lx\n", Status);
    ok(ReturnLength == sizeof(SYSTEM_KERNEL_DEBUGGER_INFORMATION), "ReturnLength = %lu\n", ReturnLength);
    ok(DebuggerInfo->KernelDebuggerEnabled == FALSE ||
       DebuggerInfo->KernelDebuggerEnabled == TRUE, "KernelDebuggerEnabled = %u\n", DebuggerInfo->KernelDebuggerEnabled);
    ok(DebuggerInfo->KernelDebuggerNotPresent == FALSE ||
       DebuggerInfo->KernelDebuggerNotPresent == TRUE, "KernelDebuggerNotPresent = %u\n", DebuggerInfo->KernelDebuggerNotPresent);

    /* Set - not supported */
    DebuggerInfo->KernelDebuggerEnabled = FALSE;
    DebuggerInfo->KernelDebuggerNotPresent = TRUE;
    Status = NtSetSystemInformation(SystemKernelDebuggerInformation, DebuggerInfo, sizeof(SYSTEM_KERNEL_DEBUGGER_INFORMATION));
    ok(Status == STATUS_INVALID_INFO_CLASS, "NtSetSystemInformation returned %lx\n", Status);
}

typedef struct _TEST_PROCESSOR_SETS
{
    ULONG Count;
    KAFFINITY Sets[8 * sizeof(KAFFINITY)];
} TEST_PROCESSOR_SETS, *PTEST_PROCESSOR_SETS;

static
ULONG
CountProcessors(
    _In_ KAFFINITY Mask)
{
    ULONG Count = 0;

    while (Mask != 0)
    {
        Mask &= Mask - 1;
        Count++;
    }

    return Count;
}

static
PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX
QueryRelationship(
    _In_ LOGICAL_PROCESSOR_RELATIONSHIP Relationship,
    _Out_ PULONG Length)
{
    PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX Info;
    ULONG Needed = 0x55555555;
    ULONG Returned = 0x55555555;
    NTSTATUS Status;

    *Length = 0;

    Status = NtQuerySystemInformationEx(SystemLogicalProcessorAndGroupInformation,
                                        &Relationship,
                                        sizeof(Relationship),
                                        NULL,
                                        0,
                                        &Needed);
    ok(Status == STATUS_INFO_LENGTH_MISMATCH, "Relationship %d: status 0x%lx\n", Relationship, Status);
    ok(Needed != 0 && Needed != 0x55555555, "Relationship %d: needs %lu bytes\n", Relationship, Needed);
    if (Status != STATUS_INFO_LENGTH_MISMATCH || Needed == 0 || Needed == 0x55555555)
        return NULL;

    Info = RtlAllocateHeap(RtlGetProcessHeap(), HEAP_ZERO_MEMORY, Needed);
    if (Info == NULL)
    {
        skip("No memory for %lu bytes\n", Needed);
        return NULL;
    }

    Status = NtQuerySystemInformationEx(SystemLogicalProcessorAndGroupInformation,
                                        &Relationship,
                                        sizeof(Relationship),
                                        Info,
                                        Needed,
                                        &Returned);
    ok(Status == STATUS_SUCCESS, "Relationship %d: status 0x%lx\n", Relationship, Status);
    ok(Returned == Needed, "Relationship %d: returned %lu bytes of %lu\n", Relationship, Returned, Needed);
    if (Status != STATUS_SUCCESS)
    {
        RtlFreeHeap(RtlGetProcessHeap(), 0, Info);
        return NULL;
    }

    *Length = Returned;
    return Info;
}

static
VOID
CollectProcessorSets(
    _In_ LOGICAL_PROCESSOR_RELATIONSHIP Relationship,
    _In_ KAFFINITY Active,
    _Out_ PTEST_PROCESSOR_SETS Sets)
{
    PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX Info, Record;
    KAFFINITY Seen = 0;
    KAFFINITY Mask;
    ULONG Length, Offset;

    Sets->Count = 0;

    Info = QueryRelationship(Relationship, &Length);
    for (Offset = 0; (Info != NULL) && (Offset < Length); Offset += Record->Size)
    {
        Record = (PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX)((PUCHAR)Info + Offset);
        ok(Record->Relationship == Relationship, "Relationship %d: record of %d\n", Relationship, Record->Relationship);
        ok(Record->Size == FIELD_OFFSET(SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX, Processor.GroupMask[1]),
           "Relationship %d: record size %lu\n", Relationship, Record->Size);
        if (Record->Size == 0)
            break;

        Mask = Record->Processor.GroupMask[0].Mask;
        ok(Record->Processor.GroupCount == 1, "Relationship %d: group count %u\n", Relationship, Record->Processor.GroupCount);
        ok(Record->Processor.GroupMask[0].Group == 0, "Relationship %d: group %u\n", Relationship, Record->Processor.GroupMask[0].Group);
        ok(Mask != 0 && (Mask & ~Active) == 0, "Relationship %d: mask 0x%I64x of 0x%I64x\n", Relationship, (ULONGLONG)Mask, (ULONGLONG)Active);
        ok((Seen & Mask) == 0, "Relationship %d: mask 0x%I64x overlaps 0x%I64x\n", Relationship, (ULONGLONG)Mask, (ULONGLONG)Seen);
        Seen |= Mask;

        if (Relationship == RelationProcessorCore)
        {
            ok(Record->Processor.Flags == ((CountProcessors(Mask) > 1) ? LTP_PC_SMT : 0),
               "Core 0x%I64x: flags 0x%x\n", (ULONGLONG)Mask, Record->Processor.Flags);
        }
        else if (Relationship == RelationProcessorPackage)
        {
            ok(Record->Processor.Flags == 0, "Package 0x%I64x: flags 0x%x\n", (ULONGLONG)Mask, Record->Processor.Flags);
        }

        if (Sets->Count < RTL_NUMBER_OF(Sets->Sets))
            Sets->Sets[Sets->Count++] = Mask;
    }
    ok(Info == NULL || Offset == Length, "Relationship %d: records end at %lu of %lu\n", Relationship, Offset, Length);
    ok(Seen == Active, "Relationship %d: sets cover 0x%I64x of 0x%I64x\n", Relationship, (ULONGLONG)Seen, (ULONGLONG)Active);

    if (Info != NULL)
        RtlFreeHeap(RtlGetProcessHeap(), 0, Info);
}

static
VOID
CheckNested(
    _In_ PCSTR Name,
    _In_ PTEST_PROCESSOR_SETS Inner,
    _In_ PTEST_PROCESSOR_SETS Outer)
{
    ULONG i, j, Containing;

    for (i = 0; i < Inner->Count; i++)
    {
        Containing = 0;
        for (j = 0; j < Outer->Count; j++)
        {
            if ((Inner->Sets[i] & ~Outer->Sets[j]) == 0)
                Containing++;
        }
        ok(Containing == 1, "%s 0x%I64x: in %lu outer sets\n", Name, (ULONGLONG)Inner->Sets[i], Containing);
    }
}

static
VOID
CheckSetSizes(
    _In_ PCSTR Name,
    _In_ PTEST_PROCESSOR_SETS Sets,
    _In_ ULONG Count,
    _In_ ULONG Processors)
{
    ULONG i;

    ok(Sets->Count == Count, "%s: %lu sets, expected %lu\n", Name, Sets->Count, Count);
    for (i = 0; i < Sets->Count; i++)
    {
        ok(CountProcessors(Sets->Sets[i]) == Processors,
           "%s 0x%I64x: %lu processors, expected %lu\n", Name, (ULONGLONG)Sets->Sets[i], CountProcessors(Sets->Sets[i]), Processors);
    }
}

static
ULONG
FindProcessorSet(
    _In_ PTEST_PROCESSOR_SETS Sets,
    _In_ KAFFINITY Mask)
{
    ULONG i;

    for (i = 0; i < Sets->Count; i++)
    {
        if (Sets->Sets[i] & Mask)
            return i;
    }

    return MAXULONG;
}

/* The older class and the CPU sets describe the same cores as the relationship records */
static
VOID
CheckCoreViews(
    _In_ PTEST_PROCESSOR_SETS Cores)
{
    PSYSTEM_LOGICAL_PROCESSOR_INFORMATION Legacy;
    PSYSTEM_CPU_SET_INFORMATION CpuSets, A, B;
    ULONG Length = 0, CoreRecords = 0, Core, i, j;
    HANDLE Process = NULL;
    NTSTATUS Status;

    Status = NtQuerySystemInformation(SystemLogicalProcessorInformation, NULL, 0, &Length);
    ok(Status == STATUS_INFO_LENGTH_MISMATCH, "SystemLogicalProcessorInformation: status 0x%lx\n", Status);
    Legacy = (Length != 0) ? RtlAllocateHeap(RtlGetProcessHeap(), HEAP_ZERO_MEMORY, Length) : NULL;
    if (Legacy != NULL)
    {
        Status = NtQuerySystemInformation(SystemLogicalProcessorInformation, Legacy, Length, &Length);
        ok(Status == STATUS_SUCCESS, "SystemLogicalProcessorInformation: status 0x%lx\n", Status);
        for (i = 0; (Status == STATUS_SUCCESS) && (i < Length / sizeof(*Legacy)); i++)
        {
            if (Legacy[i].Relationship != RelationProcessorCore)
                continue;

            CoreRecords++;
            Core = FindProcessorSet(Cores, Legacy[i].ProcessorMask);
            ok(Core != MAXULONG && Cores->Sets[Core] == Legacy[i].ProcessorMask,
               "Legacy core 0x%I64x is not a core\n", (ULONGLONG)Legacy[i].ProcessorMask);
            ok(Legacy[i].ProcessorCore.Flags == ((CountProcessors(Legacy[i].ProcessorMask) > 1) ? LTP_PC_SMT : 0),
               "Legacy core 0x%I64x: flags 0x%x\n", (ULONGLONG)Legacy[i].ProcessorMask, Legacy[i].ProcessorCore.Flags);
        }
        ok(CoreRecords == Cores->Count, "Legacy: %lu cores, %lu relationship cores\n", CoreRecords, Cores->Count);
        RtlFreeHeap(RtlGetProcessHeap(), 0, Legacy);
    }

    Length = 0;
    Status = NtQuerySystemInformationEx(SystemCpuSetInformation, &Process, sizeof(Process), NULL, 0, &Length);
    ok(Status == STATUS_BUFFER_TOO_SMALL, "SystemCpuSetInformation: status 0x%lx\n", Status);
    CpuSets = (Length != 0) ? RtlAllocateHeap(RtlGetProcessHeap(), HEAP_ZERO_MEMORY, Length) : NULL;
    if (CpuSets == NULL)
        return;

    Status = NtQuerySystemInformationEx(SystemCpuSetInformation, &Process, sizeof(Process), CpuSets, Length, &Length);
    ok(Status == STATUS_SUCCESS, "SystemCpuSetInformation: status 0x%lx\n", Status);
    for (i = 0; (Status == STATUS_SUCCESS) && (i < Length / sizeof(*CpuSets)); i++)
    {
        A = &CpuSets[i];
        for (j = 0; j < Length / sizeof(*CpuSets); j++)
        {
            B = &CpuSets[j];
            ok((A->CpuSet.CoreIndex == B->CpuSet.CoreIndex) ==
               (FindProcessorSet(Cores, (KAFFINITY)1 << A->CpuSet.LogicalProcessorIndex) ==
                FindProcessorSet(Cores, (KAFFINITY)1 << B->CpuSet.LogicalProcessorIndex)),
               "CPU sets %lu and %lu: core %u and %u\n", A->CpuSet.Id, B->CpuSet.Id, A->CpuSet.CoreIndex, B->CpuSet.CoreIndex);
        }
    }
    RtlFreeHeap(RtlGetProcessHeap(), 0, CpuSets);
}

/* LIBERNT_TEST_CPU_TOPOLOGY gives the QEMU -smp topology of the run, as "sockets=2,dies=1,cores=2,threads=2" */
static
void
Test_LogicalProcessorRelationships(void)
{
    static const LOGICAL_PROCESSOR_RELATIONSHIP Kinds[] =
    {
        RelationProcessorCore, RelationNumaNode, RelationCache, RelationProcessorPackage,
        RelationGroup, RelationProcessorDie, RelationNumaNodeEx, RelationProcessorModule
    };
    PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX Records[RTL_NUMBER_OF(Kinds)];
    PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX Info, Record;
    TEST_PROCESSOR_SETS Cores, Modules, Dies, Packages;
    ULONG Lengths[RTL_NUMBER_OF(Kinds)];
    SYSTEM_BASIC_INFORMATION Basic;
    LOGICAL_PROCESSOR_RELATIONSHIP Relationship;
    ULONG Sockets = 0, DiesPerSocket = 0, CoresPerDie = 0, Threads = 0;
    ULONG Length, Offset, Needed, i;
    CHAR Topology[128];
    KAFFINITY Active;
    NTSTATUS Status;

    Status = NtQuerySystemInformation(SystemBasicInformation, &Basic, sizeof(Basic), NULL);
    ok(Status == STATUS_SUCCESS, "SystemBasicInformation: status 0x%lx\n", Status);
    Active = Basic.ActiveProcessorsAffinityMask;

    for (i = 0; i < RTL_NUMBER_OF(Kinds); i++)
    {
        Records[i] = QueryRelationship(Kinds[i], &Lengths[i]);
        Relationship = (Kinds[i] == RelationNumaNodeEx) ? RelationNumaNode : Kinds[i];
        for (Offset = 0; (Records[i] != NULL) && (Offset < Lengths[i]); Offset += Record->Size)
        {
            Record = (PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX)((PUCHAR)Records[i] + Offset);
            ok(Record->Relationship == Relationship, "Relationship %d: record of %d\n", Kinds[i], Record->Relationship);
            ok(Record->Size != 0, "Relationship %d: empty record\n", Kinds[i]);
            if (Record->Size == 0)
                break;

            if (Record->Relationship == RelationNumaNode)
                ok(Record->NumaNode.GroupCount == 1, "Node %lu: group count %u\n", Record->NumaNode.NodeNumber, Record->NumaNode.GroupCount);
            else if (Record->Relationship == RelationCache)
                ok(Record->Cache.GroupCount == 1, "Cache L%u: group count %u\n", Record->Cache.Level, Record->Cache.GroupCount);
        }
    }

    /* With one group a node's full affinity is its affinity in that group */
    ok(Lengths[6] == Lengths[1], "NumaNodeEx: %lu bytes, NumaNode %lu\n", Lengths[6], Lengths[1]);
    if (Records[6] != NULL && Records[1] != NULL && Lengths[6] == Lengths[1])
        ok(!memcmp(Records[6], Records[1], Lengths[1]), "NumaNodeEx records differ from NumaNode\n");

    /* RelationAll is every relationship but the dies and the full NUMA view */
    Info = QueryRelationship(RelationAll, &Length);
    ok(Length == Lengths[0] + Lengths[1] + Lengths[2] + Lengths[3] + Lengths[4] + Lengths[7],
       "RelationAll: %lu bytes\n", Length);
    if (Info != NULL)
        RtlFreeHeap(RtlGetProcessHeap(), 0, Info);

    for (i = 0; i < RTL_NUMBER_OF(Kinds); i++)
    {
        if (Records[i] != NULL)
            RtlFreeHeap(RtlGetProcessHeap(), 0, Records[i]);
    }

    Relationship = RelationProcessorModule + 1;
    Needed = 0x55555555;
    Status = NtQuerySystemInformationEx(SystemLogicalProcessorAndGroupInformation,
                                        &Relationship,
                                        sizeof(Relationship),
                                        NULL,
                                        0,
                                        &Needed);
    ok(Status == STATUS_UNSUCCESSFUL, "Unknown relationship: status 0x%lx\n", Status);

    CollectProcessorSets(RelationProcessorCore, Active, &Cores);
    CollectProcessorSets(RelationProcessorModule, Active, &Modules);
    CollectProcessorSets(RelationProcessorDie, Active, &Dies);
    CollectProcessorSets(RelationProcessorPackage, Active, &Packages);
    CheckCoreViews(&Cores);
    CheckNested("Core", &Cores, &Modules);
    CheckNested("Module", &Modules, &Dies);
    CheckNested("Die", &Dies, &Packages);

    if (!GetEnvironmentVariableA("LIBERNT_TEST_CPU_TOPOLOGY", Topology, sizeof(Topology)) ||
        sscanf(Topology, "sockets=%lu,dies=%lu,cores=%lu,threads=%lu", &Sockets, &DiesPerSocket, &CoresPerDie, &Threads) != 4 ||
        Sockets == 0 || DiesPerSocket == 0 || CoresPerDie == 0 || Threads == 0)
    {
        skip("No machine topology given\n");
        return;
    }

    ok(CountProcessors(Active) == Sockets * DiesPerSocket * CoresPerDie * Threads,
       "%lu active processors for %s\n", CountProcessors(Active), Topology);
    CheckSetSizes("Core", &Cores, Sockets * DiesPerSocket * CoresPerDie, Threads);
    CheckSetSizes("Module", &Modules, Sockets * DiesPerSocket * CoresPerDie, Threads);
    CheckSetSizes("Die", &Dies, Sockets * DiesPerSocket, CoresPerDie * Threads);
    CheckSetSizes("Package", &Packages, Sockets, DiesPerSocket * CoresPerDie * Threads);
}

START_TEST(NtSystemInformation)
{
    NTSTATUS Status;
    ULONG ReturnLength;

    Status = NtQuerySystemInformation(9999, NULL, 0, NULL);
    ok(Status == STATUS_INVALID_INFO_CLASS, "NtQuerySystemInformation returned %lx\n", Status);

    Status = NtQuerySystemInformation(9999, NULL, 0, (PVOID)1);
    ok(Status == STATUS_ACCESS_VIOLATION ||
       ntv6(Status == STATUS_INVALID_INFO_CLASS), "NtQuerySystemInformation returned %lx\n", Status);

    ReturnLength = 0x55555555;
    Status = NtQuerySystemInformation(9999, NULL, 0, &ReturnLength);
    ok(Status == STATUS_INVALID_INFO_CLASS, "NtQuerySystemInformation returned %lx\n", Status);
    ok(ReturnLength == 0 ||
       ntv6(ReturnLength == 0x55555555), "ReturnLength = %lu\n", ReturnLength);

    ReturnLength = 0x55555555;
    Status = NtQuerySystemInformation(9999, NULL, 1, &ReturnLength);
    ok(Status == STATUS_ACCESS_VIOLATION ||
       ntv6(Status == STATUS_INVALID_INFO_CLASS), "NtQuerySystemInformation returned %lx\n", Status);
    ok(ReturnLength == 0x55555555, "ReturnLength = %lu\n", ReturnLength);

    ReturnLength = 0x55555555;
    Status = NtQuerySystemInformation(9999, (PVOID)1, 1, &ReturnLength);
    ok(Status == STATUS_DATATYPE_MISALIGNMENT ||
       ntv6(Status == STATUS_INVALID_INFO_CLASS), "NtQuerySystemInformation returned %lx\n", Status);
    ok(ReturnLength == 0x55555555, "ReturnLength = %lu\n", ReturnLength);

    Status = NtQuerySystemInformation(9999, NULL, 1, (PVOID)1);
    ok(Status == STATUS_ACCESS_VIOLATION ||
       ntv6(Status == STATUS_INVALID_INFO_CLASS), "NtQuerySystemInformation returned %lx\n", Status);

    Status = NtQuerySystemInformation(9999, (PVOID)1, 1, (PVOID)1);
    ok(Status == STATUS_DATATYPE_MISALIGNMENT ||
       ntv6(Status == STATUS_INVALID_INFO_CLASS), "NtQuerySystemInformation returned %lx\n", Status);

    Test_Flags();
    Test_TimeAdjustment();
    Test_KernelDebugger();
    Test_LogicalProcessorRelationships();
}
