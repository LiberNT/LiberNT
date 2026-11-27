/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test the per-process extension of the SystemProcessInformation answers
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

typedef struct _TEST_PROCESS_EXTENSION
{
    ULONGLONG DiskCounters[5];
    ULONGLONG ContextSwitches;
    ULONG Flags;
    ULONG UserSidOffset;
    ULONG PackageFullNameOffset;
    ULONGLONG EnergyValues[0x110 / sizeof(ULONGLONG)];
    ULONG AppIdOffset;
    SIZE_T SharedCommitCharge;
    ULONG JobObjectId;
    ULONG SpareUlong;
    ULONGLONG ProcessSequenceNumber;
} TEST_PROCESS_EXTENSION, *PTEST_PROCESS_EXTENSION;

#ifdef _WIN64
C_ASSERT(FIELD_OFFSET(TEST_PROCESS_EXTENSION, Flags) == 0x30);
C_ASSERT(FIELD_OFFSET(TEST_PROCESS_EXTENSION, SharedCommitCharge) == 0x158);
C_ASSERT(FIELD_OFFSET(TEST_PROCESS_EXTENSION, ProcessSequenceNumber) == 0x168);
C_ASSERT(sizeof(TEST_PROCESS_EXTENSION) == 0x170);
#endif

static
PSYSTEM_PROCESS_INFORMATION
QueryProcesses(
    _In_ SYSTEM_INFORMATION_CLASS Class)
{
    PVOID Buffer = NULL;
    ULONG Size = 0x10000, Needed = 0;
    NTSTATUS Status;

    for (;;)
    {
        Buffer = RtlAllocateHeap(RtlGetProcessHeap(), 0, Size);
        if (Buffer == NULL) return NULL;
        Status = NtQuerySystemInformation(Class, Buffer, Size, &Needed);
        if (Status != STATUS_INFO_LENGTH_MISMATCH) break;
        RtlFreeHeap(RtlGetProcessHeap(), 0, Buffer);
        Size = Needed + 0x10000;
    }
    ok(Status == STATUS_SUCCESS, "Class %d: status 0x%lx\n", Class, Status);
    if (NT_SUCCESS(Status)) return Buffer;
    RtlFreeHeap(RtlGetProcessHeap(), 0, Buffer);
    return NULL;
}

static
PSYSTEM_PROCESS_INFORMATION
FindProcess(
    _In_ PSYSTEM_PROCESS_INFORMATION Process,
    _In_ ULONG_PTR ProcessId)
{
    for (;;)
    {
        if ((ULONG_PTR)Process->UniqueProcessId == ProcessId) return Process;
        if (Process->NextEntryOffset == 0) return NULL;
        Process = (PSYSTEM_PROCESS_INFORMATION)((PUCHAR)Process + Process->NextEntryOffset);
    }
}

static
PTEST_PROCESS_EXTENSION
ExtensionOf(
    _In_ PSYSTEM_PROCESS_INFORMATION Process,
    _In_ ULONG ThreadSize)
{
    return (PTEST_PROCESS_EXTENSION)((PUCHAR)(Process + 1) + Process->NumberOfThreads * ThreadSize);
}

static
VOID
CheckSnapshots(
    _In_ SYSTEM_INFORMATION_CLASS Class,
    _In_ ULONG ThreadSize,
    _In_ PSYSTEM_PROCESS_INFORMATION First,
    _In_ PSYSTEM_PROCESS_INFORMATION Second)
{
    PSYSTEM_PROCESS_INFORMATION Own, System, Again;
    PTEST_PROCESS_EXTENSION Extension, SystemExtension;
    PSYSTEM_THREAD_INFORMATION Thread;
    ULONGLONG Switches = 0;
    ULONG i;

    Own = FindProcess(First, (ULONG_PTR)NtCurrentTeb()->ClientId.UniqueProcess);
    System = FindProcess(First, 4);
    Again = FindProcess(Second, (ULONG_PTR)NtCurrentTeb()->ClientId.UniqueProcess);
    ok(Own != NULL && System != NULL && Again != NULL, "Class %d: processes not found\n", Class);
    if (Own == NULL || System == NULL || Again == NULL) return;

    Extension = ExtensionOf(Own, ThreadSize);
    SystemExtension = ExtensionOf(System, ThreadSize);
    if (Class == SystemFullProcessInformation)
        ok((PUCHAR)Own->ImageName.Buffer >= (PUCHAR)(Extension + 1), "Class %d: name at %p, extension ends at %p\n",
           Class, Own->ImageName.Buffer, Extension + 1);
    else
        ok((PUCHAR)Own->ImageName.Buffer == (PUCHAR)(Extension + 1), "Class %d: name at %p, extension ends at %p\n",
           Class, Own->ImageName.Buffer, Extension + 1);
    ok((PUCHAR)(Extension + 1) <= (PUCHAR)Own + Own->NextEntryOffset || Own->NextEntryOffset == 0,
       "Class %d: extension past the record\n", Class);

    ok(Extension->ProcessSequenceNumber > SystemExtension->ProcessSequenceNumber, "Class %d: sequence %I64u, system %I64u\n",
       Class, Extension->ProcessSequenceNumber, SystemExtension->ProcessSequenceNumber);
    ok(Extension->ProcessSequenceNumber == ExtensionOf(Again, ThreadSize)->ProcessSequenceNumber, "Class %d: sequence changed\n", Class);
    ok(SystemExtension->ProcessSequenceNumber == 1, "Class %d: system sequence %I64u\n", Class, SystemExtension->ProcessSequenceNumber);
    ok(((SystemExtension->Flags >> 1) & 0xF) == 1, "Class %d: system flags 0x%lx\n", Class, SystemExtension->Flags);
    ok(((Extension->Flags >> 1) & 0xF) == 0, "Class %d: flags 0x%lx\n", Class, Extension->Flags);
    if (Class != SystemFullProcessInformation)
    {
        ok(Extension->UserSidOffset == 0, "Class %d: user SID offset %lu\n", Class, Extension->UserSidOffset);
        ok(Extension->PackageFullNameOffset == 0, "Class %d: package offset %lu\n", Class, Extension->PackageFullNameOffset);
        ok(Extension->AppIdOffset == 0, "Class %d: app id offset %lu\n", Class, Extension->AppIdOffset);
    }

    for (i = 0; i < Own->NumberOfThreads; i++)
    {
        Thread = (PSYSTEM_THREAD_INFORMATION)((PUCHAR)(Own + 1) + i * ThreadSize);
        Switches += Thread->ContextSwitches;
    }
    ok(Extension->ContextSwitches >= Switches, "Class %d: %I64u context switches, threads have %I64u\n",
       Class, Extension->ContextSwitches, Switches);
}

static
VOID
CheckClass(
    _In_ SYSTEM_INFORMATION_CLASS Class,
    _In_ ULONG ThreadSize)
{
    PSYSTEM_PROCESS_INFORMATION First = QueryProcesses(Class);
    PSYSTEM_PROCESS_INFORMATION Second = QueryProcesses(Class);

    if (First != NULL && Second != NULL)
        CheckSnapshots(Class, ThreadSize, First, Second);
    if (First != NULL) RtlFreeHeap(RtlGetProcessHeap(), 0, First);
    if (Second != NULL) RtlFreeHeap(RtlGetProcessHeap(), 0, Second);
}

START_TEST(SystemProcessExtension)
{
    CheckClass(SystemProcessInformation, sizeof(SYSTEM_THREAD_INFORMATION));
    CheckClass(SystemExtendedProcessInformation, sizeof(SYSTEM_EXTENDED_THREAD_INFORMATION));
    CheckClass(SystemFullProcessInformation, sizeof(SYSTEM_EXTENDED_THREAD_INFORMATION));
}
