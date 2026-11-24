/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests the stack placement of processes started from relocatable and relocation-stripped images
 * COPYRIGHT:   Copyright 2026 Ostap Brehin <osbre@protonmail.com>
 */

#include "precomp.h"

#include <ndk/psfuncs.h>

static BOOL
WriteImageCopy(PCWSTR Source, PCWSTR Destination, BOOL StripRelocations, ULONG_PTR *PreferredBase)
{
    HANDLE File, Mapping;
    PUCHAR View;
    PIMAGE_NT_HEADERS NtHeaders;
    BOOL Result = FALSE;

    if (!CopyFileW(Source, Destination, FALSE))
        return FALSE;
    File = CreateFileW(Destination, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (File == INVALID_HANDLE_VALUE)
        return FALSE;
    Mapping = CreateFileMappingW(File, NULL, PAGE_READWRITE, 0, 0, NULL);
    if (Mapping)
    {
        View = MapViewOfFile(Mapping, FILE_MAP_WRITE, 0, 0, 0);
        if (View)
        {
            NtHeaders = (PIMAGE_NT_HEADERS)(View + ((PIMAGE_DOS_HEADER)View)->e_lfanew);
            if (StripRelocations)
                NtHeaders->FileHeader.Characteristics |= IMAGE_FILE_RELOCS_STRIPPED;
            *PreferredBase = (ULONG_PTR)NtHeaders->OptionalHeader.ImageBase;
            Result = TRUE;
            UnmapViewOfFile(View);
        }
        CloseHandle(Mapping);
    }
    CloseHandle(File);
    return Result;
}

static BOOL
QueryChildLayout(PCWSTR Image, ULONG_PTR *StackBase, ULONG_PTR *ImageBase)
{
    STARTUPINFOW StartupInfo = { sizeof(StartupInfo) };
    PROCESS_INFORMATION ProcessInfo;
    THREAD_BASIC_INFORMATION ThreadInfo;
    PROCESS_BASIC_INFORMATION ProcessBasic;
    NT_TIB Tib;
    PVOID PebImageBase;
    WCHAR CommandLine[MAX_PATH + 32];
    NTSTATUS Status;
    BOOL Result = FALSE;

    StringCchPrintfW(CommandLine, ARRAYSIZE(CommandLine), L"\"%s\" StrippedImageLayout", Image);
    if (!CreateProcessW(Image, CommandLine, NULL, NULL, FALSE, CREATE_SUSPENDED, NULL, NULL, &StartupInfo, &ProcessInfo))
    {
        ok(0, "CreateProcessW(%ls) failed with %lu\n", Image, GetLastError());
        return FALSE;
    }

    Status = NtQueryInformationThread(ProcessInfo.hThread, ThreadBasicInformation, &ThreadInfo, sizeof(ThreadInfo), NULL);
    ok(NT_SUCCESS(Status), "NtQueryInformationThread returned 0x%08lx\n", Status);
    if (NT_SUCCESS(Status) &&
        ReadProcessMemory(ProcessInfo.hProcess, ThreadInfo.TebBaseAddress, &Tib, sizeof(Tib), NULL))
    {
        Status = NtQueryInformationProcess(ProcessInfo.hProcess, ProcessBasicInformation, &ProcessBasic,
                                           sizeof(ProcessBasic), NULL);
        ok(NT_SUCCESS(Status), "NtQueryInformationProcess returned 0x%08lx\n", Status);
        if (NT_SUCCESS(Status) &&
            ReadProcessMemory(ProcessInfo.hProcess, (PUCHAR)ProcessBasic.PebBaseAddress + FIELD_OFFSET(PEB, ImageBaseAddress),
                              &PebImageBase, sizeof(PebImageBase), NULL))
        {
            *StackBase = (ULONG_PTR)Tib.StackBase;
            *ImageBase = (ULONG_PTR)PebImageBase;
            Result = TRUE;
        }
    }
    ok(Result, "cannot read the layout of %ls: %lu\n", Image, GetLastError());

    TerminateProcess(ProcessInfo.hProcess, 0);
    WaitForSingleObject(ProcessInfo.hProcess, 5000);
    CloseHandle(ProcessInfo.hThread);
    CloseHandle(ProcessInfo.hProcess);
    return Result;
}

START_TEST(StrippedImageLayout)
{
    WCHAR Source[MAX_PATH], Directory[MAX_PATH], Relocatable[MAX_PATH], Stripped[MAX_PATH];
    ULONG_PTR PreferredBase, StackBase[2], ImageBase[2], RelocatableStack, RelocatableImage;
    char **Arguments;
    ULONG i;

    if (winetest_get_mainargs(&Arguments) >= 3)
        ExitProcess(0);

    if (!GetModuleFileNameW(NULL, Source, ARRAYSIZE(Source)) ||
        !GetTempPathW(ARRAYSIZE(Directory), Directory) ||
        FAILED(StringCchPrintfW(Relocatable, ARRAYSIZE(Relocatable), L"%sreloc_image_%lu.exe",
                                Directory, GetCurrentProcessId())) ||
        FAILED(StringCchPrintfW(Stripped, ARRAYSIZE(Stripped), L"%sstripped_image_%lu.exe",
                                Directory, GetCurrentProcessId())))
    {
        skip("no room for the image copies: %lu\n", GetLastError());
        return;
    }

    if (!WriteImageCopy(Source, Stripped, TRUE, &PreferredBase))
    {
        skip("cannot write the stripped image: %lu\n", GetLastError());
        return;
    }
    for (i = 0; i < ARRAYSIZE(StackBase); i++)
    {
        StackBase[i] = ImageBase[i] = 0;
        QueryChildLayout(Stripped, &StackBase[i], &ImageBase[i]);
        ok(ImageBase[i] == PreferredBase, "stripped image mapped at %p, not at its preferred base %p\n",
           (PVOID)ImageBase[i], (PVOID)PreferredBase);
        ok(StackBase[i] != 0 && StackBase[i] <= MAXULONG, "stripped image stack base %p is above 4 GB\n",
           (PVOID)StackBase[i]);
    }
    ok(StackBase[0] == StackBase[1], "stripped image stack base moved from %p to %p\n",
       (PVOID)StackBase[0], (PVOID)StackBase[1]);
    DeleteFileW(Stripped);

    if (sizeof(PVOID) < sizeof(ULONG64))
        return;
    if (!WriteImageCopy(Source, Relocatable, FALSE, &PreferredBase))
    {
        skip("cannot write the relocatable image: %lu\n", GetLastError());
        return;
    }
    RelocatableStack = RelocatableImage = 0;
    if (QueryChildLayout(Relocatable, &RelocatableStack, &RelocatableImage))
    {
        ok(RelocatableStack > MAXULONG, "high-entropy image stack base %p is below 4 GB\n", (PVOID)RelocatableStack);
        ok(RelocatableImage != PreferredBase, "high-entropy image stayed at its preferred base %p\n",
           (PVOID)RelocatableImage);
    }
    DeleteFileW(Relocatable);
}
