/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test FsRtlQueryKernelEaFile, FsRtlSetKernelEaFile and the kernel EA protection of the file system
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>

static
ULONG
BuildEa(
    _Out_writes_bytes_(64) PUCHAR Buffer,
    _In_ PCSTR Name,
    _In_ PCSTR Value)
{
    PFILE_FULL_EA_INFORMATION Ea = (PFILE_FULL_EA_INFORMATION)Buffer;
    SIZE_T NameLength = strlen(Name);
    SIZE_T ValueLength = strlen(Value);

    ASSERT(FIELD_OFFSET(FILE_FULL_EA_INFORMATION, EaName) + NameLength + 1 + ValueLength <= 64);
    RtlZeroMemory(Buffer, 64);
    Ea->EaNameLength = (UCHAR)NameLength;
    Ea->EaValueLength = (USHORT)ValueLength;
    RtlCopyMemory(Ea->EaName, Name, NameLength + 1);
    RtlCopyMemory(Ea->EaName + NameLength + 1, Value, ValueLength);
    return (ULONG)(FIELD_OFFSET(FILE_FULL_EA_INFORMATION, EaName) + NameLength + 1 + ValueLength);
}

static
VOID
CheckKernelEa(
    _In_ PFILE_OBJECT FileObject,
    _In_opt_ PCSTR ExpectedValue,
    _In_ PCSTR Label)
{
    UCHAR List[32], Get[64];
    PFILE_GET_EA_INFORMATION Want = (PFILE_GET_EA_INFORMATION)List;
    PFILE_FULL_EA_INFORMATION Ea = (PFILE_FULL_EA_INFORMATION)Get;
    ULONG Returned = 0xFFFFFFFF;
    NTSTATUS Status;

    RtlZeroMemory(List, sizeof(List));
    Want->EaNameLength = 11;
    RtlCopyMemory(Want->EaName, "$Kernel.Kmt", 12);
    RtlFillMemory(Get, sizeof(Get), 0xCC);
    Status = FsRtlQueryKernelEaFile(FileObject, Get, sizeof(Get), FALSE, List,
                                    FIELD_OFFSET(FILE_GET_EA_INFORMATION, EaName) + 12, NULL, TRUE, &Returned);
    ok(Status == STATUS_SUCCESS, "%s: query 0x%08lx\n", Label, Status);
    if (!NT_SUCCESS(Status))
        return;
    ok(Returned != 0 && Returned <= sizeof(Get), "%s: %lu bytes returned\n", Label, Returned);
    ok(Ea->EaNameLength == 11 && _strnicmp(Ea->EaName, "$KERNEL.KMT", 11) == 0, "%s: name %.*s\n", Label, Ea->EaNameLength, Ea->EaName);
    if (ExpectedValue == NULL)
    {
        ok(Ea->EaValueLength == 0, "%s: the kernel EA still has a %u-byte value\n", Label, Ea->EaValueLength);
        return;
    }
    ok(Ea->EaValueLength == strlen(ExpectedValue) &&
       RtlCompareMemory(Ea->EaName + Ea->EaNameLength + 1, ExpectedValue, Ea->EaValueLength) == Ea->EaValueLength,
       "%s: value length %u, expected '%s'\n", Label, Ea->EaValueLength, ExpectedValue);
}

static
VOID
TestFileEas(VOID)
{
    UNICODE_STRING Path = RTL_CONSTANT_STRING(L"\\SystemRoot\\libernt_kernelea.tmp");
    OBJECT_ATTRIBUTES Attributes;
    IO_STATUS_BLOCK Iosb;
    PFILE_OBJECT FileObject;
    PFILE_FULL_EA_INFORMATION Ea;
    UCHAR Set[64], Get[128];
    ULONG Returned, Length;
    HANDLE File;
    NTSTATUS Status;

    InitializeObjectAttributes(&Attributes, &Path, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);
    Status = ZwCreateFile(&File, GENERIC_READ | GENERIC_WRITE | DELETE | SYNCHRONIZE, &Attributes, &Iosb, NULL, FILE_ATTRIBUTE_NORMAL, 0,
                          FILE_OVERWRITE_IF, FILE_SYNCHRONOUS_IO_NONALERT | FILE_NON_DIRECTORY_FILE | FILE_DELETE_ON_CLOSE, NULL, 0);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        return;
    Status = ObReferenceObjectByHandle(File, 0, *IoFileObjectType, KernelMode, (PVOID *)&FileObject, NULL);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
    {
        ZwClose(File);
        return;
    }

    Length = BuildEa(Set, "PROBE", "abc");
    Status = FsRtlSetKernelEaFile(FileObject, Set, Length);
    ok_eq_hex(Status, STATUS_SUCCESS);

    RtlFillMemory(Get, sizeof(Get), 0xCC);
    Returned = 0xFFFFFFFF;
    Status = FsRtlQueryKernelEaFile(FileObject, Get, sizeof(Get), FALSE, NULL, 0, NULL, TRUE, &Returned);
    ok_eq_hex(Status, STATUS_SUCCESS);
    Ea = (PFILE_FULL_EA_INFORMATION)Get;
    ok(Returned >= Length && Returned <= sizeof(Get), "%lu bytes returned\n", Returned);
    ok(Ea->EaNameLength == 5 && RtlCompareMemory(Ea->EaName, "PROBE", 5) == 5, "name %.*s\n", Ea->EaNameLength, Ea->EaName);
    ok(Ea->EaValueLength == 3 && RtlCompareMemory(Ea->EaName + 6, "abc", 3) == 3, "value length %u\n", Ea->EaValueLength);

    Returned = 0xFFFFFFFF;
    Status = FsRtlQueryKernelEaFile(FileObject, Get, sizeof(Get), FALSE, NULL, 0, NULL, FALSE, &Returned);
    ok_eq_hex(Status, STATUS_NO_MORE_EAS);

    Length = BuildEa(Set, "$Kernel.Kmt", "xyz");
    Status = FsRtlSetKernelEaFile(FileObject, Set, Length);
    ok_eq_hex(Status, STATUS_SUCCESS);
    CheckKernelEa(FileObject, "xyz", "after the kernel call set it");

    Length = BuildEa(Set, "$Kernel.Kmt", "new");
    Status = ZwSetEaFile(File, &Iosb, Set, Length);
    ok_eq_hex(Status, STATUS_SUCCESS);
    CheckKernelEa(FileObject, "xyz", "after a plain set request");

    Length = BuildEa(Set, "$Kernel.Kmt", "");
    Status = ZwSetEaFile(File, &Iosb, Set, Length);
    ok_eq_hex(Status, STATUS_SUCCESS);
    CheckKernelEa(FileObject, "xyz", "after a plain delete request");

    Length = BuildEa(Set, "$kernel.kmt", "low");
    Status = FsRtlSetKernelEaFile(FileObject, Set, Length);
    ok_eq_hex(Status, STATUS_SUCCESS);
    CheckKernelEa(FileObject, "low", "after a lowercase kernel call");

    Length = BuildEa(Set, "$Kernel.Kmt", "");
    Status = FsRtlSetKernelEaFile(FileObject, Set, Length);
    ok_eq_hex(Status, STATUS_SUCCESS);
    CheckKernelEa(FileObject, NULL, "after the kernel call deleted it");

    ObDereferenceObject(FileObject);
    ZwClose(File);
}

static
VOID
TestDirectDeviceOpen(VOID)
{
    UNICODE_STRING Path = RTL_CONSTANT_STRING(L"\\Device\\Null");
    OBJECT_ATTRIBUTES Attributes;
    IO_STATUS_BLOCK Iosb;
    PFILE_OBJECT FileObject;
    UCHAR Buffer[64];
    ULONG Returned = 0xFFFFFFFF;
    HANDLE Handle;
    NTSTATUS Status;

    InitializeObjectAttributes(&Attributes, &Path, OBJ_KERNEL_HANDLE, NULL, NULL);
    Status = ZwOpenFile(&Handle, FILE_READ_ATTRIBUTES | SYNCHRONIZE, &Attributes, &Iosb, FILE_SHARE_READ | FILE_SHARE_WRITE, 0);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        return;
    Status = ObReferenceObjectByHandle(Handle, 0, *IoFileObjectType, KernelMode, (PVOID *)&FileObject, NULL);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
    {
        ok(FileObject->Flags & FO_DIRECT_DEVICE_OPEN, "flags 0x%08lx\n", FileObject->Flags);
        Status = FsRtlQueryKernelEaFile(FileObject, Buffer, sizeof(Buffer), FALSE, NULL, 0, NULL, TRUE, &Returned);
        ok_eq_hex(Status, STATUS_INVALID_DEVICE_REQUEST);
        ok_eq_ulong(Returned, 0xFFFFFFFFUL);
        Status = FsRtlSetKernelEaFile(FileObject, Buffer, BuildEa(Buffer, "PROBE", "abc"));
        ok_eq_hex(Status, STATUS_INVALID_DEVICE_REQUEST);
        ObDereferenceObject(FileObject);
    }
    ZwClose(Handle);
}

START_TEST(FsRtlKernelEa)
{
    TestFileEas();
    TestDirectDeviceOpen();
}
