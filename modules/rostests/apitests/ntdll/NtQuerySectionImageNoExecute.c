/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test that NtQuerySection clears SEC_NOCACHE on SEC_IMAGE_NO_EXECUTE sections
 * COPYRIGHT:   Copyright 2026 Jean-Luc Robitaille <jlr@robitaille.us>
 */

#include "precomp.h"

START_TEST(NtQuerySectionImageNoExecute)
{
    WCHAR Path[MAX_PATH];
    UNICODE_STRING NtPath;
    OBJECT_ATTRIBUTES ObjectAttributes;
    IO_STATUS_BLOCK IoStatus;
    HANDLE FileHandle = NULL;
    HANDLE NoExecSection = NULL;
    HANDLE ImageSection = NULL;
    SECTION_BASIC_INFORMATION NoExecInfo;
    SECTION_BASIC_INFORMATION ImageInfo;
    SIZE_T ReturnLength;
    NTSTATUS Status;
    UINT Length;

    Length = GetSystemDirectoryW(Path, ARRAYSIZE(Path));
    ok(Length != 0 && Length + ARRAYSIZE(L"\\kernel32.dll") <= ARRAYSIZE(Path),
       "GetSystemDirectoryW returned invalid length %u\n", Length);
    if (Length == 0 || Length + ARRAYSIZE(L"\\kernel32.dll") > ARRAYSIZE(Path))
        return;

    wcscpy(Path + Length, L"\\kernel32.dll");
    if (!RtlDosPathNameToNtPathName_U(Path, &NtPath, NULL, NULL))
    {
        ok(0, "RtlDosPathNameToNtPathName_U failed\n");
        return;
    }

    InitializeObjectAttributes(&ObjectAttributes, &NtPath, OBJ_CASE_INSENSITIVE, NULL, NULL);
    Status = NtOpenFile(&FileHandle,
                        FILE_READ_DATA | SYNCHRONIZE,
                        &ObjectAttributes,
                        &IoStatus,
                        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                        FILE_SYNCHRONOUS_IO_NONALERT | FILE_NON_DIRECTORY_FILE);
    RtlFreeUnicodeString(&NtPath);
    ok_ntstatus(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        return;

    Status = NtCreateSection(&NoExecSection,
                             SECTION_QUERY | SECTION_MAP_READ,
                             NULL,
                             NULL,
                             PAGE_READONLY,
                             SEC_IMAGE_NO_EXECUTE,
                             FileHandle);
    ok_ntstatus(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        goto Cleanup;

    ZeroMemory(&NoExecInfo, sizeof(NoExecInfo));
    ReturnLength = 0;
    Status = NtQuerySection(NoExecSection,
                            SectionBasicInformation,
                            &NoExecInfo,
                            sizeof(NoExecInfo),
                            &ReturnLength);
    ok_ntstatus(Status, STATUS_SUCCESS);
    ok_eq_size(ReturnLength, sizeof(NoExecInfo));
    ok_eq_pointer(NoExecInfo.BaseAddress, NULL);
    ok_eq_hex(NoExecInfo.Attributes, SEC_FILE | SEC_IMAGE);
    ok((NoExecInfo.Attributes & SEC_NOCACHE) == 0,
       "SEC_NOCACHE set in BasicInfo.Attributes: 0x%lx\n", NoExecInfo.Attributes);

    Status = NtCreateSection(&ImageSection,
                             SECTION_QUERY | SECTION_MAP_READ,
                             NULL,
                             NULL,
                             PAGE_READONLY,
                             SEC_IMAGE,
                             FileHandle);
    ok_ntstatus(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        goto Cleanup;

    ZeroMemory(&ImageInfo, sizeof(ImageInfo));
    ReturnLength = 0;
    Status = NtQuerySection(ImageSection,
                            SectionBasicInformation,
                            &ImageInfo,
                            sizeof(ImageInfo),
                            &ReturnLength);
    ok_ntstatus(Status, STATUS_SUCCESS);
    ok_eq_size(ReturnLength, sizeof(ImageInfo));
    ok_eq_pointer(ImageInfo.BaseAddress, NULL);
    ok_eq_hex(ImageInfo.Attributes, SEC_FILE | SEC_IMAGE);
    ok_eq_hex(NoExecInfo.Attributes, ImageInfo.Attributes);
    ok_eq_hex64(NoExecInfo.Size.QuadPart, ImageInfo.Size.QuadPart);

Cleanup:
    if (ImageSection != NULL)
        NtClose(ImageSection);
    if (NoExecSection != NULL)
        NtClose(NoExecSection);
    if (FileHandle != NULL)
        NtClose(FileHandle);
}
