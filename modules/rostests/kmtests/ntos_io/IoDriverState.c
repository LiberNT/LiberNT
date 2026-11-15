/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test the state directories and registry keys a driver opens with IoGetDriverDirectory, IoGetDeviceDirectory and IoOpenDriverRegistryKey
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>

#define NDEBUG
#include <debug.h>

#define SENTINEL_HANDLE ((HANDLE)(ULONG_PTR)0xA5A5A5A5)
#define DIRECTORY_ACCESS (FILE_LIST_DIRECTORY | SYNCHRONIZE)

static const DEVPROPKEY DeviceDirectoryKey =
    { { 0x83da6326, 0x97a6, 0x4088, { 0x94, 0x53, 0xa1, 0x92, 0x3f, 0x57, 0x3b, 0x29 } }, 21 };

static UNICODE_STRING ServiceName;

static
BOOLEAN
EndsWith(
    _In_ PCUNICODE_STRING String,
    _In_ PCUNICODE_STRING Suffix)
{
    UNICODE_STRING Tail;

    if (String->Length < Suffix->Length)
        return FALSE;
    Tail.Buffer = String->Buffer + (String->Length - Suffix->Length) / sizeof(WCHAR);
    Tail.Length = Tail.MaximumLength = Suffix->Length;
    return RtlEqualUnicodeString(&Tail, Suffix, TRUE);
}

static
VOID
CheckHandle(
    _In_ HANDLE Handle,
    _In_ PCWSTR Suffix,
    _In_opt_ PCUNICODE_STRING Middle,
    _In_ ACCESS_MASK Access,
    _Out_opt_ PUNICODE_STRING NameCopy)
{
    static ULONG Buffer[512];
    POBJECT_NAME_INFORMATION Name = (POBJECT_NAME_INFORMATION)Buffer;
    OBJECT_BASIC_INFORMATION Basic;
    WCHAR Expected[256];
    UNICODE_STRING ExpectedString;
    ULONG Needed;
    NTSTATUS Status;

    RtlZeroMemory(Buffer, sizeof(Buffer));
    Status = ZwQueryObject(Handle, ObjectNameInformation, Buffer, sizeof(Buffer) - sizeof(WCHAR), &Needed);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        return;

    RtlStringCbPrintfW(Expected, sizeof(Expected), Suffix, Middle);
    RtlInitUnicodeString(&ExpectedString, Expected);
    ok(EndsWith(&Name->Name, &ExpectedString), "%wZ does not end with %wZ\n", &Name->Name, &ExpectedString);

    Status = ZwQueryObject(Handle, ObjectBasicInformation, &Basic, sizeof(Basic), &Needed);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
        ok_eq_hex(Basic.GrantedAccess, Access);

    if (NameCopy)
    {
        NameCopy->Length = 0;
        RtlCopyUnicodeString(NameCopy, &Name->Name);
    }
}

static
VOID
TestDriverDirectory(VOID)
{
    PLDR_DATA_TABLE_ENTRY Entry = KmtDriverObject->DriverSection;
    UNICODE_STRING ImageDirectory;
    HANDLE Handle;
    NTSTATUS Status;

    Handle = SENTINEL_HANDLE;
    Status = IoGetDriverDirectory(NULL, DriverDirectoryData, 0, &Handle);
    ok_eq_hex(Status, STATUS_INVALID_PARAMETER);
    ok_eq_pointer(Handle, SENTINEL_HANDLE);
    Status = IoGetDriverDirectory(KmtDriverObject, DriverDirectoryData, 0, NULL);
    ok_eq_hex(Status, STATUS_INVALID_PARAMETER);
    Status = IoGetDriverDirectory(KmtDriverObject, DriverDirectoryData, 1, &Handle);
    ok_eq_hex(Status, STATUS_INVALID_PARAMETER);
    ok_eq_pointer(Handle, SENTINEL_HANDLE);
    Status = IoGetDriverDirectory(KmtDriverObject, (DRIVER_DIRECTORY_TYPE)3, 0, &Handle);
    ok_eq_hex(Status, STATUS_INVALID_PARAMETER);
    ok_eq_pointer(Handle, SENTINEL_HANDLE);

    Status = IoGetDriverDirectory(KmtDriverObject, DriverDirectoryData, 0, &Handle);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
    {
        CheckHandle(Handle, L"\\ServiceState\\%wZ\\Data", &ServiceName, DIRECTORY_ACCESS, NULL);
        ZwClose(Handle);
    }

    Status = IoGetDriverDirectory(KmtDriverObject, DriverDirectorySharedData, 0, &Handle);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
    {
        CheckHandle(Handle, L"\\ServiceState\\%wZ\\SharedData", &ServiceName, DIRECTORY_ACCESS, NULL);
        ZwClose(Handle);
    }

    ok(Entry != NULL, "The test driver has no loader entry\n");
    if (Entry == NULL)
        return;

    ImageDirectory = Entry->FullDllName;
    while (ImageDirectory.Length && ImageDirectory.Buffer[ImageDirectory.Length / sizeof(WCHAR) - 1] != L'\\')
        ImageDirectory.Length -= sizeof(WCHAR);
    if (ImageDirectory.Length)
        ImageDirectory.Length -= sizeof(WCHAR);
    while (ImageDirectory.Length && ImageDirectory.Buffer[0] != L'\\')
    {
        ImageDirectory.Buffer++;
        ImageDirectory.Length -= sizeof(WCHAR);
    }
    while (ImageDirectory.Length > sizeof(WCHAR))
    {
        PWCHAR Separator = ImageDirectory.Buffer + 1;
        while (Separator < ImageDirectory.Buffer + ImageDirectory.Length / sizeof(WCHAR) && *Separator != L'\\')
            Separator++;
        if (Separator == ImageDirectory.Buffer + ImageDirectory.Length / sizeof(WCHAR))
            break;
        ImageDirectory.Length -= (USHORT)((Separator - ImageDirectory.Buffer) * sizeof(WCHAR));
        ImageDirectory.Buffer = Separator;
    }
    ImageDirectory.MaximumLength = ImageDirectory.Length;

    Status = IoGetDriverDirectory(KmtDriverObject, DriverDirectoryImage, 0, &Handle);
    if (ImageDirectory.Length && ImageDirectory.Buffer[ImageDirectory.Length / sizeof(WCHAR) - 1] == L':')
    {
        ok_eq_hex(Status, STATUS_NOT_A_DIRECTORY);
        if (NT_SUCCESS(Status))
            ZwClose(Handle);
        return;
    }
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
    {
        CheckHandle(Handle, L"%wZ", &ImageDirectory, DIRECTORY_ACCESS, NULL);
        ZwClose(Handle);
    }
}

static
VOID
TestDriverRegistryKey(VOID)
{
    OBJECT_ATTRIBUTES Attributes;
    UNICODE_STRING Path;
    WCHAR PathBuffer[160];
    HANDLE Handle;
    NTSTATUS Status;

    Handle = SENTINEL_HANDLE;
    Status = IoOpenDriverRegistryKey(KmtDriverObject, DriverRegKeyPersistentState, KEY_READ, 1, &Handle);
    ok_eq_hex(Status, STATUS_INVALID_PARAMETER);
    ok_eq_pointer(Handle, SENTINEL_HANDLE);
    Status = IoOpenDriverRegistryKey(KmtDriverObject, (DRIVER_REGKEY_TYPE)3, KEY_READ, 0, &Handle);
    ok_eq_hex(Status, STATUS_INVALID_PARAMETER);
    ok_eq_pointer(Handle, SENTINEL_HANDLE);

    Status = IoOpenDriverRegistryKey(KmtDriverObject, DriverRegKeyPersistentState, KEY_READ, 0, &Handle);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
    {
        CheckHandle(Handle, L"\\Services\\%wZ\\State", &ServiceName, KEY_READ, NULL);
        ZwClose(Handle);
    }

    Status = IoOpenDriverRegistryKey(KmtDriverObject, DriverRegKeySharedPersistentState, KEY_READ, 0, &Handle);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
    {
        CheckHandle(Handle, L"\\Services\\%wZ\\SharedState", &ServiceName, KEY_READ, NULL);
        ZwClose(Handle);
    }

    RtlStringCbPrintfW(PathBuffer, sizeof(PathBuffer),
                       L"\\Registry\\Machine\\System\\CurrentControlSet\\Services\\%wZ\\Parameters", &ServiceName);
    RtlInitUnicodeString(&Path, PathBuffer);
    InitializeObjectAttributes(&Attributes, &Path, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);
    Status = IoOpenDriverRegistryKey(KmtDriverObject, DriverRegKeyParameters, KEY_READ, 0, &Handle);
    if (NT_SUCCESS(Status))
    {
        CheckHandle(Handle, L"\\Services\\%wZ\\Parameters", &ServiceName, KEY_READ, NULL);
        ZwClose(Handle);
        return;
    }

    ok_eq_hex(Status, STATUS_OBJECT_NAME_NOT_FOUND);
    Status = ZwOpenKey(&Handle, KEY_READ, &Attributes);
    ok_eq_hex(Status, STATUS_OBJECT_NAME_NOT_FOUND);
    if (NT_SUCCESS(Status))
        ZwClose(Handle);
}

static
VOID
TestDeviceDirectory(VOID)
{
    UNICODE_STRING Disk = RTL_CONSTANT_STRING(L"\\Device\\Harddisk0\\DR0");
    UNICODE_STRING Prefix = RTL_CONSTANT_STRING(L"\\System32\\DriverState\\Devices\\{");
    UNICODE_STRING First, Second, Guid, Search;
    WCHAR FirstBuffer[260], SecondBuffer[260], Property[40];
    PDEVICE_OBJECT Top, Pdo;
    PFILE_OBJECT File;
    DEVPROPTYPE Type;
    ULONG Required, Index;
    HANDLE Handle;
    NTSTATUS Status;

    Status = IoGetDeviceObjectPointer(&Disk, FILE_READ_ATTRIBUTES, &File, &Top);
    if (!NT_SUCCESS(Status))
    {
        skip(FALSE, "No %wZ: 0x%08lx\n", &Disk, Status);
        return;
    }
    Pdo = IoGetDeviceAttachmentBaseRef(Top);

    Handle = SENTINEL_HANDLE;
    Status = IoGetDeviceDirectory(NULL, DeviceDirectoryData, 0, NULL, &Handle);
    ok_eq_hex(Status, STATUS_INVALID_PARAMETER);
    Status = IoGetDeviceDirectory(Pdo, DeviceDirectoryData, 0, NULL, NULL);
    ok_eq_hex(Status, STATUS_INVALID_PARAMETER);
    Status = IoGetDeviceDirectory(Pdo, (DEVICE_DIRECTORY_TYPE)1, 0, NULL, &Handle);
    ok_eq_hex(Status, STATUS_INVALID_PARAMETER);
    Status = IoGetDeviceDirectory(Pdo, DeviceDirectoryData, 1, NULL, &Handle);
    ok_eq_hex(Status, STATUS_INVALID_PARAMETER);
    Status = IoGetDeviceDirectory(Pdo, DeviceDirectoryData, 0, (PVOID)1, &Handle);
    ok_eq_hex(Status, STATUS_INVALID_PARAMETER);
    if (Top != Pdo)
    {
        Status = IoGetDeviceDirectory(Top, DeviceDirectoryData, 0, NULL, &Handle);
        ok_eq_hex(Status, STATUS_INVALID_PARAMETER);
    }
    ok_eq_pointer(Handle, SENTINEL_HANDLE);

    RtlInitEmptyUnicodeString(&First, FirstBuffer, sizeof(FirstBuffer));
    RtlInitEmptyUnicodeString(&Second, SecondBuffer, sizeof(SecondBuffer));
    Status = IoGetDeviceDirectory(Pdo, DeviceDirectoryData, 0, NULL, &Handle);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
    {
        CheckHandle(Handle, L"}\\Data", NULL, DIRECTORY_ACCESS, &First);
        ZwClose(Handle);
    }
    Status = IoGetDeviceDirectory(Pdo, DeviceDirectoryData, 0, NULL, &Handle);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
    {
        CheckHandle(Handle, L"}\\Data", NULL, DIRECTORY_ACCESS, &Second);
        ZwClose(Handle);
    }
    ok(RtlEqualUnicodeString(&First, &Second, TRUE), "%wZ changed to %wZ\n", &First, &Second);

    Guid.Buffer = NULL;
    for (Index = 0; Index + Prefix.Length / sizeof(WCHAR) + 38 <= First.Length / sizeof(WCHAR); Index++)
    {
        Search.Buffer = First.Buffer + Index;
        Search.Length = Search.MaximumLength = Prefix.Length;
        if (RtlEqualUnicodeString(&Search, &Prefix, TRUE))
        {
            Guid.Buffer = First.Buffer + Index + Prefix.Length / sizeof(WCHAR) - 1;
            Guid.Length = Guid.MaximumLength = 38 * sizeof(WCHAR);
            break;
        }
    }
    ok(Guid.Buffer != NULL, "%wZ is not under DriverState\\Devices\n", &First);

    RtlZeroMemory(Property, sizeof(Property));
    Type = DEVPROP_TYPE_EMPTY;
    Required = 0;
    Status = IoGetDevicePropertyData(Pdo, &DeviceDirectoryKey, 0, 0, sizeof(Property), Property, &Required, &Type);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_ulong(Type, (ULONG)DEVPROP_TYPE_STRING);
    ok_eq_ulong(Required, 39UL * sizeof(WCHAR));
    if (Guid.Buffer != NULL)
        ok(!_wcsnicmp(Property, Guid.Buffer, 38), "Property %ls, directory %wZ\n", Property, &Guid);

    ObDereferenceObject(Pdo);
    ObDereferenceObject(File);
}

START_TEST(IoDriverState)
{
    UNICODE_STRING DriverName = KmtDriverObject->DriverName;
    USHORT Index;

    for (Index = DriverName.Length / sizeof(WCHAR); Index > 0 && DriverName.Buffer[Index - 1] != L'\\'; Index--);
    ServiceName.Buffer = DriverName.Buffer + Index;
    ServiceName.Length = ServiceName.MaximumLength = DriverName.Length - Index * sizeof(WCHAR);
    trace("Service %wZ\n", &ServiceName);

    TestDriverRegistryKey();
    TestDriverDirectory();
    TestDeviceDirectory();
}
