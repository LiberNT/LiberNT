/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests for NtQueryDirectoryObject
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

static
HANDLE
OpenRootDirectory(VOID)
{
    UNICODE_STRING Name = RTL_CONSTANT_STRING(L"\\");
    OBJECT_ATTRIBUTES ObjectAttributes;
    HANDLE Handle = NULL;
    NTSTATUS Status;

    InitializeObjectAttributes(&ObjectAttributes, &Name, OBJ_CASE_INSENSITIVE, NULL, NULL);
    Status = NtOpenDirectoryObject(&Handle, DIRECTORY_QUERY, &ObjectAttributes);
    ok_hex(Status, STATUS_SUCCESS);

    return NT_SUCCESS(Status) ? Handle : NULL;
}

START_TEST(NtQueryDirectoryObject)
{
    POBJECT_DIRECTORY_INFORMATION Info;
    HANDLE Directory;
    BOOLEAN FoundTypes = FALSE;
    ULONG Context, PreviousContext;
    ULONG ReturnLength;
    ULONG Entries = 0;
    NTSTATUS Status;
    UCHAR Small[8];
    PVOID Buffer;

    Directory = OpenRootDirectory();
    if (Directory == NULL) return;

    Buffer = RtlAllocateHeap(RtlGetProcessHeap(), 0, 0x2000);
    ok(Buffer != NULL, "no buffer\n");
    if (Buffer == NULL)
    {
        NtClose(Directory);
        return;
    }

    /* A buffer that cannot hold one entry reports how much a single entry needs */
    Context = 0;
    ReturnLength = 0;
    Status = NtQueryDirectoryObject(Directory, Small, sizeof(Small), TRUE, TRUE,
                                    &Context, &ReturnLength);
    ok_hex(Status, STATUS_BUFFER_TOO_SMALL);
    ok(ReturnLength > sizeof(Small), "ReturnLength %lu\n", ReturnLength);

    /* The root directory always holds the object type directory */
    Context = 0;
    ReturnLength = 0;
    Status = NtQueryDirectoryObject(Directory, Buffer, 0x2000, FALSE, TRUE,
                                    &Context, &ReturnLength);
    ok(Status == STATUS_SUCCESS || Status == STATUS_MORE_ENTRIES,
       "NtQueryDirectoryObject returned %lx\n", Status);
    if (Status == STATUS_SUCCESS || Status == STATUS_MORE_ENTRIES)
    {
        ok(Context != 0, "context did not advance\n");
        ok(ReturnLength != 0, "no length reported\n");

        for (Info = Buffer; Info->Name.Length != 0; Info++)
        {
            Entries++;
            if (Info->Name.Buffer[Info->Name.Length / sizeof(WCHAR)] != UNICODE_NULL)
                ok(FALSE, "entry name is not terminated\n");
            if (Info->TypeName.Buffer[Info->TypeName.Length / sizeof(WCHAR)] != UNICODE_NULL)
                ok(FALSE, "entry type name is not terminated\n");
            if (Info->Name.Length == wcslen(L"ObjectTypes") * sizeof(WCHAR) &&
                _wcsnicmp(Info->Name.Buffer, L"ObjectTypes",
                          Info->Name.Length / sizeof(WCHAR)) == 0)
            {
                FoundTypes = TRUE;
                ok(_wcsnicmp(Info->TypeName.Buffer, L"Directory",
                             Info->TypeName.Length / sizeof(WCHAR)) == 0,
                   "ObjectTypes is not a Directory\n");
            }
        }

        ok(Entries != 0, "no entries returned\n");
        ok_bool_true(FoundTypes, "ObjectTypes found");
    }

    /* One entry at a time walks the same directory without repeating */
    Context = 0;
    Status = NtQueryDirectoryObject(Directory, Buffer, 0x2000, TRUE, TRUE,
                                    &Context, NULL);
    ok_hex(Status, STATUS_SUCCESS);
    PreviousContext = Context;
    if (NT_SUCCESS(Status))
    {
        Status = NtQueryDirectoryObject(Directory, Buffer, 0x2000, TRUE, FALSE,
                                        &Context, NULL);
        ok_hex(Status, STATUS_SUCCESS);
        ok(Context > PreviousContext, "context %lu did not advance past %lu\n",
           Context, PreviousContext);
    }

    /* Walking past the end reports that the directory is exhausted */
    Context = 0x10000;
    Status = NtQueryDirectoryObject(Directory, Buffer, 0x2000, FALSE, FALSE,
                                    &Context, NULL);
    ok_hex(Status, STATUS_NO_MORE_ENTRIES);

    RtlFreeHeap(RtlGetProcessHeap(), 0, Buffer);
    NtClose(Directory);
}
