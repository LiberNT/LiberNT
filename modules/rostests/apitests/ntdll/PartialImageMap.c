/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test partial SEC_IMAGE mappings and MemoryImageInformation ImagePartialMap
 * COPYRIGHT:   Copyright 2026 Jean-Luc Robitaille <jlr@robitaille.us>
 */

#include "precomp.h"

static
HANDLE
OpenSystemImage(
    _In_ PCWSTR ImageName)
{
    WCHAR Path[MAX_PATH];
    UNICODE_STRING NtPath;
    OBJECT_ATTRIBUTES ObjectAttributes;
    IO_STATUS_BLOCK IoStatusBlock;
    HANDLE FileHandle;
    UINT Length;
    SIZE_T NameLength;
    NTSTATUS Status;

    Length = GetSystemDirectoryW(Path, ARRAYSIZE(Path));
    NameLength = wcslen(ImageName);
    if (Length == 0 || Length + 1 + NameLength >= ARRAYSIZE(Path))
    {
        ok(0, "GetSystemDirectoryW failed: %u\n", Length);
        return NULL;
    }

    Path[Length] = L'\\';
    wcscpy(Path + Length + 1, ImageName);

    if (!RtlDosPathNameToNtPathName_U(Path, &NtPath, NULL, NULL))
    {
        ok(0, "RtlDosPathNameToNtPathName_U failed\n");
        return NULL;
    }

    InitializeObjectAttributes(&ObjectAttributes, &NtPath, OBJ_CASE_INSENSITIVE, NULL, NULL);
    Status = NtOpenFile(&FileHandle,
                        FILE_READ_DATA | FILE_EXECUTE | SYNCHRONIZE,
                        &ObjectAttributes,
                        &IoStatusBlock,
                        FILE_SHARE_READ | FILE_SHARE_DELETE,
                        FILE_SYNCHRONOUS_IO_NONALERT | FILE_NON_DIRECTORY_FILE);
    RtlFreeUnicodeString(&NtPath);
    ok(Status == STATUS_SUCCESS, "NtOpenFile failed: 0x%lx\n", Status);
    if (!NT_SUCCESS(Status))
        return NULL;

    return FileHandle;
}

static
void
TestImageMapping(
    _In_ HANDLE FileHandle,
    _In_ ULONG64 SectionMaximumSize,
    _In_ SIZE_T RequestedViewSize,
    _In_ SIZE_T ExpectedViewSize,
    _In_ ULONG ExpectedPartialMap,
    _In_ PCSTR CaseName)
{
    HANDLE SectionHandle;
    LARGE_INTEGER MaximumSize;
    PLARGE_INTEGER MaxSizePtr = NULL;
    PVOID BaseAddress = NULL;
    SIZE_T ViewSize = RequestedViewSize;
    MEMORY_IMAGE_INFORMATION ImageInfo;
    SIZE_T Returned = 0;
    NTSTATUS Status;

    if (SectionMaximumSize != 0)
    {
        MaximumSize.QuadPart = (LONGLONG)SectionMaximumSize;
        MaxSizePtr = &MaximumSize;
    }

    Status = NtCreateSection(&SectionHandle,
                             SECTION_ALL_ACCESS,
                             NULL,
                             MaxSizePtr,
                             PAGE_READONLY,
                             SEC_IMAGE,
                             FileHandle);
    ok(Status == STATUS_SUCCESS, "%s: NtCreateSection failed: 0x%lx\n", CaseName, Status);
    if (!NT_SUCCESS(Status))
        return;

    Status = NtMapViewOfSection(SectionHandle,
                                NtCurrentProcess(),
                                &BaseAddress,
                                0,
                                0,
                                NULL,
                                &ViewSize,
                                ViewUnmap,
                                0,
                                PAGE_READONLY);
    ok(NT_SUCCESS(Status), "%s: NtMapViewOfSection failed: 0x%lx\n", CaseName, Status);
    if (NT_SUCCESS(Status))
    {
        if (ExpectedViewSize != 0)
        {
            ok(ViewSize == ExpectedViewSize,
               "%s: ViewSize = %Iu, expected %Iu\n",
               CaseName, (size_t)ViewSize, (size_t)ExpectedViewSize);
        }
        else
        {
            ok(ViewSize > 0x1000, "%s: ViewSize = %Iu, expected > 4096\n", CaseName, (size_t)ViewSize);
        }

        RtlFillMemory(&ImageInfo, sizeof(ImageInfo), 0xcc);
        Status = NtQueryVirtualMemory(NtCurrentProcess(),
                                      BaseAddress,
                                      MemoryImageInformation,
                                      &ImageInfo,
                                      sizeof(ImageInfo),
                                      &Returned);
        ok(Status == STATUS_SUCCESS, "%s: NtQueryVirtualMemory failed: 0x%lx\n", CaseName, Status);
        if (NT_SUCCESS(Status))
        {
            ok(Returned == sizeof(ImageInfo),
               "%s: Returned = %Iu, expected %Iu\n",
               CaseName, (size_t)Returned, sizeof(ImageInfo));
            ok(ImageInfo.ImageBase == BaseAddress,
               "%s: ImageBase = %p, expected %p\n",
               CaseName, ImageInfo.ImageBase, BaseAddress);
            ok(ImageInfo.SizeOfImage > 0x1000,
               "%s: SizeOfImage = %Iu, expected > 4096\n",
               CaseName, (size_t)ImageInfo.SizeOfImage);
            ok(ImageInfo.ImagePartialMap == ExpectedPartialMap,
               "%s: ImagePartialMap = %lu, expected %lu\n",
               CaseName, (ULONG)ImageInfo.ImagePartialMap, ExpectedPartialMap);
        }

        Status = NtUnmapViewOfSection(NtCurrentProcess(), BaseAddress);
        ok(Status == STATUS_SUCCESS, "%s: NtUnmapViewOfSection failed: 0x%lx\n", CaseName, Status);
    }

    Status = NtClose(SectionHandle);
    ok(Status == STATUS_SUCCESS, "%s: NtClose(Section) failed: 0x%lx\n", CaseName, Status);
}

START_TEST(PartialImageMap)
{
    HANDLE FileHandle;
    NTSTATUS Status;

    FileHandle = OpenSystemImage(L"kernel32.dll");
    if (FileHandle == NULL)
        return;

    TestImageMapping(FileHandle, 0x1000, 0, 0x1000, 1, "section MaximumSize=0x1000");
    TestImageMapping(FileHandle, 0, 0x1000, 0x1000, 1, "map ViewSize=0x1000");
    TestImageMapping(FileHandle, 0, 0, 0, 0, "full image");

    Status = NtClose(FileHandle);
    ok(Status == STATUS_SUCCESS, "NtClose(File) failed: 0x%lx\n", Status);
}
