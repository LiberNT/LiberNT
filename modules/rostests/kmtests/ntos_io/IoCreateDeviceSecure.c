/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     IoCreateDeviceSecure applies the SDDL string, or the overrides its device class carries
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>
#include <wdmsec.h>

#define DEVICE_NAME L"\\Device\\KmtIoCreateDeviceSecure"
#define CLASS_KEY L"\\Registry\\Machine\\System\\CurrentControlSet\\Control\\Class\\{6f1d5a9e-2b8c-4f37-9a41-d3c25e7b8f60}"

static const GUID KmtTestClassGuid =
    { 0x6f1d5a9e, 0x2b8c, 0x4f37, { 0x9a, 0x41, 0xd3, 0xc2, 0x5e, 0x7b, 0x8f, 0x60 } };

typedef struct _EXPECTED_ACE
{
    PSID Sid;
    ACCESS_MASK Access;
} EXPECTED_ACE, *PEXPECTED_ACE;

static
PDEVICE_OBJECT
CreateSecureDevice(
    _In_ PCUNICODE_STRING Sddl,
    _In_opt_ LPCGUID ClassGuid,
    _Out_ PNTSTATUS Status)
{
    UNICODE_STRING DeviceName = RTL_CONSTANT_STRING(DEVICE_NAME);
    PDEVICE_OBJECT Device = NULL;

    *Status = IoCreateDeviceSecure(KmtDriverObject,
                                   0,
                                   &DeviceName,
                                   FILE_DEVICE_UNKNOWN,
                                   FILE_DEVICE_SECURE_OPEN,
                                   FALSE,
                                   Sddl,
                                   ClassGuid,
                                   &Device);

    return NT_SUCCESS(*Status) ? Device : NULL;
}

static
VOID
CheckDacl(
    _In_ PDEVICE_OBJECT Device,
    _In_ ULONG Count,
    _In_reads_(Count) const EXPECTED_ACE *Expected)
{
    PSECURITY_DESCRIPTOR Descriptor = NULL;
    BOOLEAN Allocated = FALSE;
    BOOLEAN Defaulted, Present = FALSE;
    PACCESS_ALLOWED_ACE Ace;
    ACCESS_MASK Access;
    PSID Owner = NULL;
    PACL Dacl = NULL;
    NTSTATUS Status;
    ULONG Index;

    Status = ObGetObjectSecurity(Device, &Descriptor, &Allocated);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status) || Descriptor == NULL) return;

    Status = RtlGetOwnerSecurityDescriptor(Descriptor, &Owner, &Defaulted);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok(Owner != NULL, "the device has no owner\n");

    Status = RtlGetDaclSecurityDescriptor(Descriptor, &Present, &Dacl, &Defaulted);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok(Present && Dacl != NULL, "the device has no DACL\n");

    if (Present && Dacl != NULL)
    {
        ok_eq_ulong((ULONG)Dacl->AceCount, Count);

        for (Index = 0; Index < Count && Index < Dacl->AceCount; Index++)
        {
            Status = RtlGetAce(Dacl, Index, (PVOID *)&Ace);
            ok_eq_hex(Status, STATUS_SUCCESS);
            if (!NT_SUCCESS(Status)) break;

            ok_eq_uint(Ace->Header.AceType, ACCESS_ALLOWED_ACE_TYPE);
            ok(RtlEqualSid(&Ace->SidStart, Expected[Index].Sid), "entry %lu names another SID\n", Index);

            Access = Ace->Mask;
            RtlMapGenericMask(&Access, IoGetFileObjectGenericMapping());
            ok(Access == Expected[Index].Access, "entry %lu grants 0x%lx, expected 0x%lx\n",
               Index, Access, Expected[Index].Access);
        }
    }

    ObReleaseObjectSecurity(Descriptor, Allocated);
}

static
VOID
TestSddl(VOID)
{
    EXPECTED_ACE Expected[3];
    PDEVICE_OBJECT Device;
    NTSTATUS Status;

    Expected[0].Sid = SeExports->SeLocalSystemSid;
    Expected[0].Access = FILE_ALL_ACCESS;
    Expected[1].Sid = SeExports->SeAliasAdminsSid;
    Expected[1].Access = FILE_GENERIC_READ | FILE_GENERIC_WRITE | FILE_GENERIC_EXECUTE;
    Expected[2].Sid = SeExports->SeWorldSid;
    Expected[2].Access = FILE_GENERIC_READ;

    Device = CreateSecureDevice(&SDDL_DEVOBJ_SYS_ALL_ADM_RWX_WORLD_R, NULL, &Status);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (Device != NULL)
    {
        ok_eq_ulong(Device->DeviceType, (ULONG)FILE_DEVICE_UNKNOWN);
        CheckDacl(Device, RTL_NUMBER_OF(Expected), Expected);
        IoDeleteDevice(Device);
    }

    Device = CreateSecureDevice(&SDDL_DEVOBJ_KERNEL_ONLY, NULL, &Status);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (Device != NULL)
    {
        CheckDacl(Device, 0, NULL);
        IoDeleteDevice(Device);
    }
}

static
VOID
TestBadSddl(VOID)
{
    UNICODE_STRING DeviceName = RTL_CONSTANT_STRING(DEVICE_NAME);
    UNICODE_STRING CutShort = RTL_CONSTANT_STRING(L"D:P(A;;GA;;;SY");
    PFILE_OBJECT FileObject;
    PDEVICE_OBJECT Device;
    NTSTATUS Status;

    Device = CreateSecureDevice(&CutShort, NULL, &Status);
    ok(!NT_SUCCESS(Status), "an SDDL string cut short made a device (0x%lx)\n", Status);
    if (Device != NULL) IoDeleteDevice(Device);

    Status = IoGetDeviceObjectPointer(&DeviceName, FILE_READ_ATTRIBUTES, &FileObject, &Device);
    ok_eq_hex(Status, STATUS_OBJECT_NAME_NOT_FOUND);
    if (NT_SUCCESS(Status)) ObDereferenceObject(FileObject);
}

static
NTSTATUS
SetDwordValue(
    _In_ HANDLE Key,
    _In_ PCWSTR Name,
    _In_ ULONG Data)
{
    UNICODE_STRING ValueName;

    RtlInitUnicodeString(&ValueName, Name);
    return ZwSetValueKey(Key, &ValueName, 0, REG_DWORD, &Data, sizeof(Data));
}

static
NTSTATUS
SetAdminsOnlySecurity(
    _In_ HANDLE Key)
{
    UNICODE_STRING ValueName = RTL_CONSTANT_STRING(L"Security");
    UCHAR AclBuffer[sizeof(ACL) + sizeof(ACCESS_ALLOWED_ACE) + SECURITY_MAX_SID_SIZE];
    UCHAR Relative[SECURITY_DESCRIPTOR_MIN_LENGTH + sizeof(AclBuffer)];
    SECURITY_DESCRIPTOR Absolute;
    PACL Acl = (PACL)AclBuffer;
    ULONG Length = sizeof(Relative);
    NTSTATUS Status;

    Status = RtlCreateSecurityDescriptor(&Absolute, SECURITY_DESCRIPTOR_REVISION);
    if (NT_SUCCESS(Status)) Status = RtlCreateAcl(Acl, sizeof(AclBuffer), ACL_REVISION);
    if (NT_SUCCESS(Status))
        Status = RtlAddAccessAllowedAce(Acl, ACL_REVISION, GENERIC_ALL, SeExports->SeAliasAdminsSid);
    if (NT_SUCCESS(Status)) Status = RtlSetDaclSecurityDescriptor(&Absolute, TRUE, Acl, FALSE);
    if (NT_SUCCESS(Status)) Status = RtlAbsoluteToSelfRelativeSD(&Absolute, (PSECURITY_DESCRIPTOR)Relative, &Length);
    if (!NT_SUCCESS(Status)) return Status;

    return ZwSetValueKey(Key, &ValueName, 0, REG_BINARY, Relative, Length);
}

static
VOID
DeleteKey(
    _In_ HANDLE Key)
{
    NTSTATUS Status;

    Status = ZwDeleteKey(Key);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ZwClose(Key);
}

static
VOID
TestClassOverrides(VOID)
{
    UNICODE_STRING ClassName = RTL_CONSTANT_STRING(CLASS_KEY);
    UNICODE_STRING PropertiesName = RTL_CONSTANT_STRING(L"Properties");
    OBJECT_ATTRIBUTES Attributes;
    EXPECTED_ACE Expected[1];
    HANDLE ClassKey, PropertiesKey;
    PDEVICE_OBJECT Device;
    NTSTATUS Status;

    InitializeObjectAttributes(&Attributes, &ClassName, OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE, NULL, NULL);
    Status = ZwCreateKey(&ClassKey, KEY_ALL_ACCESS, &Attributes, 0, NULL, REG_OPTION_VOLATILE, NULL);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status)) return;

    InitializeObjectAttributes(&Attributes, &PropertiesName, OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE, ClassKey, NULL);
    Status = ZwCreateKey(&PropertiesKey, KEY_ALL_ACCESS, &Attributes, 0, NULL, REG_OPTION_VOLATILE, NULL);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
    {
        DeleteKey(ClassKey);
        return;
    }

    ok_eq_hex(SetDwordValue(PropertiesKey, L"DeviceType", FILE_DEVICE_NETWORK), STATUS_SUCCESS);
    ok_eq_hex(SetDwordValue(PropertiesKey, L"DeviceCharacteristics", FILE_DEVICE_SECURE_OPEN | FILE_READ_ONLY_DEVICE),
              STATUS_SUCCESS);
    ok_eq_hex(SetDwordValue(PropertiesKey, L"Exclusive", 1), STATUS_SUCCESS);
    ok_eq_hex(SetAdminsOnlySecurity(PropertiesKey), STATUS_SUCCESS);

    Expected[0].Sid = SeExports->SeAliasAdminsSid;
    Expected[0].Access = FILE_ALL_ACCESS;

    Device = CreateSecureDevice(&SDDL_DEVOBJ_SYS_ALL, &KmtTestClassGuid, &Status);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (Device != NULL)
    {
        ok_eq_ulong(Device->DeviceType, (ULONG)FILE_DEVICE_NETWORK);
        ok_eq_hex(Device->Characteristics, (ULONG)(FILE_DEVICE_SECURE_OPEN | FILE_READ_ONLY_DEVICE));
        ok((Device->Flags & DO_EXCLUSIVE) != 0, "the device is not exclusive (flags 0x%lx)\n", Device->Flags);
        CheckDacl(Device, RTL_NUMBER_OF(Expected), Expected);
        IoDeleteDevice(Device);
    }

    DeleteKey(PropertiesKey);

    Expected[0].Sid = SeExports->SeLocalSystemSid;

    Device = CreateSecureDevice(&SDDL_DEVOBJ_SYS_ALL, &KmtTestClassGuid, &Status);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (Device != NULL)
    {
        ok_eq_ulong(Device->DeviceType, (ULONG)FILE_DEVICE_UNKNOWN);
        ok_eq_hex(Device->Characteristics, (ULONG)FILE_DEVICE_SECURE_OPEN);
        ok((Device->Flags & DO_EXCLUSIVE) == 0, "the device is exclusive (flags 0x%lx)\n", Device->Flags);
        CheckDacl(Device, RTL_NUMBER_OF(Expected), Expected);
        IoDeleteDevice(Device);
    }

    DeleteKey(ClassKey);
}

START_TEST(IoCreateDeviceSecure)
{
    TestSddl();
    TestBadSddl();
    TestClassOverrides();
}
