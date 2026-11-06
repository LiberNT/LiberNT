/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test that system views still map while the cache holds views of a large file
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>

#define TEST_FILE_SIZE      (192LL * 1024 * 1024)
#define TEST_STRIDE         (256 * 1024)
#define TEST_SECTION_SIZE   (8 * 1024 * 1024)

START_TEST(MmSystemViewCache)
{
    UNICODE_STRING FileName = RTL_CONSTANT_STRING(L"\\SystemRoot\\kmtest-MmSystemViewCache.tmp");
    FILE_END_OF_FILE_INFORMATION EndOfFile;
    OBJECT_ATTRIBUTES ObjectAttributes;
    IO_STATUS_BLOCK IoStatusBlock;
    LARGE_INTEGER Offset, Maximum;
    HANDLE File = NULL, Section = NULL;
    PVOID SectionObject = NULL;
    PVOID View = NULL;
    SIZE_T ViewSize = 0;
    ULONG Reads = 0;
    UCHAR Byte;
    NTSTATUS Status;

    InitializeObjectAttributes(&ObjectAttributes, &FileName, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);
    Status = ZwCreateFile(&File, GENERIC_READ | GENERIC_WRITE | SYNCHRONIZE, &ObjectAttributes, &IoStatusBlock,
                          NULL, FILE_ATTRIBUTE_NORMAL, 0, FILE_SUPERSEDE,
                          FILE_SYNCHRONOUS_IO_NONALERT | FILE_NON_DIRECTORY_FILE | FILE_DELETE_ON_CLOSE, NULL, 0);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        return;

    Status = ZwFsControlFile(File, NULL, NULL, NULL, &IoStatusBlock, FSCTL_SET_SPARSE, NULL, 0, NULL, 0);
    ok_eq_hex(Status, STATUS_SUCCESS);

    EndOfFile.EndOfFile.QuadPart = TEST_FILE_SIZE;
    Status = ZwSetInformationFile(File, &IoStatusBlock, &EndOfFile, sizeof(EndOfFile), FileEndOfFileInformation);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        goto Finish;

    for (Offset.QuadPart = 0; Offset.QuadPart < TEST_FILE_SIZE; Offset.QuadPart += TEST_STRIDE)
    {
        Byte = 0xA5;
        Status = ZwReadFile(File, NULL, NULL, NULL, &IoStatusBlock, &Byte, sizeof(Byte), &Offset, NULL);
        if (!NT_SUCCESS(Status) || IoStatusBlock.Information != sizeof(Byte) || Byte != 0)
            break;
        Reads++;
    }
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_ulong(Reads, (ULONG)(TEST_FILE_SIZE / TEST_STRIDE));

    Maximum.QuadPart = TEST_SECTION_SIZE;
    Status = ZwCreateSection(&Section, SECTION_ALL_ACCESS, NULL, &Maximum, PAGE_READWRITE, SEC_COMMIT, NULL);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        goto Finish;

    Status = ObReferenceObjectByHandle(Section, SECTION_MAP_READ | SECTION_MAP_WRITE, MmSectionObjectType,
                                       KernelMode, &SectionObject, NULL);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        goto Finish;

    Status = MmMapViewInSystemSpace(SectionObject, &View, &ViewSize);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
    {
        ok_eq_size(ViewSize, (SIZE_T)TEST_SECTION_SIZE);
        KmtStartSeh();
        *(volatile ULONG *)View = 0x5EED5EED;
        *(volatile ULONG *)((PUCHAR)View + TEST_SECTION_SIZE - sizeof(ULONG)) = 0x5EED5EED;
        KmtEndSeh(STATUS_SUCCESS);
        Status = MmUnmapViewInSystemSpace(View);
        ok_eq_hex(Status, STATUS_SUCCESS);
    }

Finish:
    if (SectionObject != NULL)
        ObDereferenceObject(SectionObject);
    if (Section != NULL)
        ZwClose(Section);
    ZwClose(File);
}
