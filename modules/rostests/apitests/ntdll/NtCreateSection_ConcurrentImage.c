/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test that concurrent image section creation for one file prepares the image once
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

#define CREATORS 16

typedef struct _CREATOR
{
    PCWSTR Path;
    HANDLE Start;
    volatile LONG *Ready;
    HANDLE Section;
    NTSTATUS Status;
    ULONG64 Cpu;
} CREATOR, *PCREATOR;

static
ULONG64
ThreadCpu(VOID)
{
    THREAD_CYCLE_TIME_INFORMATION Information;

    if (!NT_SUCCESS(NtQueryInformationThread(NtCurrentThread(), ThreadCycleTime, &Information, sizeof(Information), NULL)))
        return 0;

    return Information.AccumulatedCycles;
}

static
NTSTATUS
CreateImageSection(
    _In_ PCWSTR Path,
    _Out_ PHANDLE Section,
    _Out_ PULONG64 Cpu)
{
    HANDLE File;
    NTSTATUS Status;
    ULONG64 Before;

    *Section = NULL;
    *Cpu = 0;

    File = CreateFileW(Path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, 0, NULL);
    if (File == INVALID_HANDLE_VALUE)
        return STATUS_OBJECT_NAME_NOT_FOUND;

    Before = ThreadCpu();
    Status = NtCreateSection(Section, SECTION_ALL_ACCESS, NULL, NULL, PAGE_READONLY, SEC_IMAGE, File);
    *Cpu = ThreadCpu() - Before;

    CloseHandle(File);
    return Status;
}

static
DWORD
WINAPI
CreatorThread(
    _In_ PVOID Context)
{
    PCREATOR Creator = Context;

    InterlockedIncrement(Creator->Ready);
    WaitForSingleObject(Creator->Start, INFINITE);
    Creator->Status = CreateImageSection(Creator->Path, &Creator->Section, &Creator->Cpu);
    return 0;
}

static
PVOID
ImageTransfer(
    _In_ HANDLE Section)
{
    SECTION_IMAGE_INFORMATION Information;
    NTSTATUS Status;

    Status = NtQuerySection(Section, SectionImageInformation, &Information, sizeof(Information), NULL);
    ok_ntstatus(Status, STATUS_SUCCESS);
    return NT_SUCCESS(Status) ? Information.TransferAddress : NULL;
}

START_TEST(NtCreateSection_ConcurrentImage)
{
    WCHAR Source[MAX_PATH], TempDir[MAX_PATH], Single[MAX_PATH], Shared[MAX_PATH];
    CREATOR Creators[CREATORS];
    HANDLE Threads[CREATORS];
    HANDLE Start, Section;
    volatile LONG Ready = 0;
    ULONG64 SingleCpu, TotalCpu = 0;
    PVOID Transfer = NULL;
    NTSTATUS Status;
    ULONG Length, i, Started = 0;

    Length = GetSystemDirectoryW(Source, ARRAYSIZE(Source));
    if (!Length || Length + ARRAYSIZE(L"\\shell32.dll") > ARRAYSIZE(Source))
    {
        skip("Invalid system directory length %lu\n", Length);
        return;
    }
    wcscpy(Source + Length, L"\\shell32.dll");

    ok(GetTempPathW(ARRAYSIZE(TempDir), TempDir) != 0, "GetTempPathW failed %lu\n", GetLastError());
    ok(GetTempFileNameW(TempDir, L"ics", 0, Single) != 0, "GetTempFileNameW failed %lu\n", GetLastError());
    ok(GetTempFileNameW(TempDir, L"ics", 0, Shared) != 0, "GetTempFileNameW failed %lu\n", GetLastError());
    ok(CopyFileW(Source, Single, FALSE), "CopyFileW failed %lu\n", GetLastError());
    ok(CopyFileW(Source, Shared, FALSE), "CopyFileW failed %lu\n", GetLastError());

    Status = CreateImageSection(Single, &Section, &SingleCpu);
    ok_ntstatus(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        goto Cleanup;
    NtClose(Section);

    Start = CreateEventW(NULL, TRUE, FALSE, NULL);
    ok(Start != NULL, "CreateEventW failed %lu\n", GetLastError());
    if (Start == NULL)
        goto Cleanup;

    for (i = 0; i < CREATORS; i++)
    {
        Creators[i].Path = Shared;
        Creators[i].Start = Start;
        Creators[i].Ready = &Ready;
        Creators[i].Section = NULL;
        Creators[i].Status = STATUS_PENDING;
        Creators[i].Cpu = 0;
        Threads[i] = CreateThread(NULL, 0, CreatorThread, &Creators[i], 0, NULL);
        ok(Threads[i] != NULL, "CreateThread failed %lu\n", GetLastError());
        if (Threads[i] == NULL)
            break;
        Started++;
    }

    while (Ready != (LONG)Started)
        Sleep(1);
    Sleep(50);
    SetEvent(Start);
    WaitForMultipleObjects(Started, Threads, TRUE, INFINITE);

    for (i = 0; i < Started; i++)
    {
        ok_ntstatus(Creators[i].Status, STATUS_SUCCESS);
        TotalCpu += Creators[i].Cpu;
        if (Creators[i].Section == NULL)
            continue;
        if (Transfer == NULL)
            Transfer = ImageTransfer(Creators[i].Section);
        else
            ok(ImageTransfer(Creators[i].Section) == Transfer, "Section %lu has a different image\n", i);
    }

    ok(Started == CREATORS, "Started %lu of %u creators\n", Started, CREATORS);
    ok(SingleCpu != 0, "No cycles counted for one creation\n");
    ok(TotalCpu <= 4 * SingleCpu,
       "%lu concurrent creators used %I64u cycles, one creation used %I64u cycles\n",
       Started, TotalCpu, SingleCpu);
    trace("%lu concurrent creators used %I64u cycles, one creation used %I64u cycles\n",
          Started, TotalCpu, SingleCpu);

    for (i = 0; i < Started; i++)
    {
        if (Creators[i].Section != NULL)
            NtClose(Creators[i].Section);
        CloseHandle(Threads[i]);
    }
    CloseHandle(Start);

Cleanup:
    DeleteFileW(Single);
    DeleteFileW(Shared);
}
