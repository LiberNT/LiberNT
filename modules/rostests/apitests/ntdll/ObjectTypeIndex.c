/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test that object type information and the system handle list report the same type index
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

static UCHAR TypesBuffer[1 << 20];

static
UCHAR
FindTypeIndex(
    _In_ PCUNICODE_STRING Name)
{
    POBJECT_ALL_TYPES_INFORMATION AllTypes = (POBJECT_ALL_TYPES_INFORMATION)TypesBuffer;
    PUCHAR Entry = TypesBuffer + ALIGN_UP(sizeof(*AllTypes), ULONG_PTR);
    ULONG Index;

    for (Index = 0; Index < AllTypes->NumberOfTypes; Index++)
    {
        POBJECT_TYPE_INFORMATION Type = (POBJECT_TYPE_INFORMATION)Entry;

        if (RtlEqualUnicodeString(&Type->TypeName, Name, FALSE))
            return Type->TypeIndex;
        Entry += ALIGN_UP(sizeof(*Type) + Type->TypeName.MaximumLength, ULONG_PTR);
    }
    return 0;
}

static
USHORT
FindHandleTypeIndex(
    _In_ PSYSTEM_HANDLE_INFORMATION_EX Handles,
    _In_ HANDLE Handle)
{
    ULONG_PTR Index;

    for (Index = 0; Index < Handles->NumberOfHandles; Index++)
    {
        if (Handles->Handles[Index].UniqueProcessId == GetCurrentProcessId() &&
            Handles->Handles[Index].HandleValue == (ULONG_PTR)Handle)
        {
            return Handles->Handles[Index].ObjectTypeIndex;
        }
    }
    return 0xffff;
}

START_TEST(ObjectTypeIndex)
{
    static const PCWSTR Names[] = { L"Event", L"Process", L"Thread" };
    POBJECT_ALL_TYPES_INFORMATION AllTypes = (POBJECT_ALL_TYPES_INFORMATION)TypesBuffer;
    PSYSTEM_HANDLE_INFORMATION_EX Handles;
    UCHAR Seen[256] = { 0 };
    UCHAR TypeBuffer[512];
    POBJECT_TYPE_INFORMATION Type = (POBJECT_TYPE_INFORMATION)TypeBuffer;
    PUCHAR Entry;
    HANDLE Objects[3];
    ULONG Length = 0, Index;
    SIZE_T HandlesSize = 8 << 20;
    NTSTATUS Status;

    Status = NtQueryObject(NULL, ObjectTypesInformation, TypesBuffer, sizeof(TypesBuffer), &Length);
    ok(Status == STATUS_SUCCESS, "ObjectTypesInformation returned 0x%lx\n", Status);
    if (!NT_SUCCESS(Status))
        return;
    ok(AllTypes->NumberOfTypes != 0, "no object types\n");

    Entry = TypesBuffer + ALIGN_UP(sizeof(*AllTypes), ULONG_PTR);
    for (Index = 0; Index < AllTypes->NumberOfTypes && Entry < TypesBuffer + Length; Index++)
    {
        POBJECT_TYPE_INFORMATION Current = (POBJECT_TYPE_INFORMATION)Entry;

        ok(Current->TypeIndex != 0, "%wZ has type index 0\n", &Current->TypeName);
        ok(Current->ReservedByte == 0, "%wZ has reserved byte %d\n", &Current->TypeName, Current->ReservedByte);
        ok(Seen[Current->TypeIndex] == 0, "%wZ repeats type index %u\n", &Current->TypeName, Current->TypeIndex);
        Seen[Current->TypeIndex] = 1;
        Entry += ALIGN_UP(sizeof(*Current) + Current->TypeName.MaximumLength, ULONG_PTR);
    }

    Status = NtCreateEvent(&Objects[0], EVENT_ALL_ACCESS, NULL, NotificationEvent, FALSE);
    ok(Status == STATUS_SUCCESS, "NtCreateEvent returned 0x%lx\n", Status);
    Status = NtDuplicateObject(NtCurrentProcess(), NtCurrentProcess(), NtCurrentProcess(), &Objects[1], 0, 0, DUPLICATE_SAME_ACCESS);
    ok(Status == STATUS_SUCCESS, "NtDuplicateObject returned 0x%lx\n", Status);
    Status = NtDuplicateObject(NtCurrentProcess(), NtCurrentThread(), NtCurrentProcess(), &Objects[2], 0, 0, DUPLICATE_SAME_ACCESS);
    ok(Status == STATUS_SUCCESS, "NtDuplicateObject returned 0x%lx\n", Status);

    Handles = NULL;
    Status = NtAllocateVirtualMemory(NtCurrentProcess(), (PVOID *)&Handles, 0, &HandlesSize, MEM_COMMIT, PAGE_READWRITE);
    ok(Status == STATUS_SUCCESS, "NtAllocateVirtualMemory returned 0x%lx\n", Status);
    if (!NT_SUCCESS(Status))
        return;
    Status = NtQuerySystemInformation(SystemExtendedHandleInformation, Handles, (ULONG)HandlesSize, &Length);
    ok(Status == STATUS_SUCCESS, "SystemExtendedHandleInformation returned 0x%lx\n", Status);

    for (Index = 0; Index < RTL_NUMBER_OF(Names); Index++)
    {
        UNICODE_STRING Name;
        UCHAR Expected;

        RtlInitUnicodeString(&Name, Names[Index]);
        Expected = FindTypeIndex(&Name);
        ok(Expected != 0, "%ls is not in ObjectTypesInformation\n", Names[Index]);

        RtlFillMemory(TypeBuffer, sizeof(TypeBuffer), 0xcc);
        Status = NtQueryObject(Objects[Index], ObjectTypeInformation, TypeBuffer, sizeof(TypeBuffer), &Length);
        ok(Status == STATUS_SUCCESS, "%ls: ObjectTypeInformation returned 0x%lx\n", Names[Index], Status);
        if (NT_SUCCESS(Status))
        {
            ok(Type->TypeIndex == Expected, "%ls: type index %u, expected %u\n", Names[Index], Type->TypeIndex, Expected);
            ok(Type->ReservedByte == 0, "%ls: reserved byte %d\n", Names[Index], Type->ReservedByte);
        }
        if (NT_SUCCESS(Status) && Handles->NumberOfHandles != 0)
        {
            USHORT HandleType = FindHandleTypeIndex(Handles, Objects[Index]);
            ok(HandleType == Expected, "%ls: handle list type index %u, expected %u\n", Names[Index], HandleType, Expected);
        }
        NtClose(Objects[Index]);
    }

    HandlesSize = 0;
    NtFreeVirtualMemory(NtCurrentProcess(), (PVOID *)&Handles, &HandlesSize, MEM_RELEASE);
}
