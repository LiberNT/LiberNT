/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     A device object's security reads back, a DACL set on it keeps its owner and group, and its pool comes back
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>

#define CYCLES 256

static
PDEVICE_OBJECT
CreateNamedDevice(
    _In_ PCWSTR Name)
{
    UNICODE_STRING DeviceName;
    PDEVICE_OBJECT Device = NULL;
    NTSTATUS Status;

    RtlInitUnicodeString(&DeviceName, Name);
    Status = IoCreateDevice(KmtDriverObject,
                            0,
                            &DeviceName,
                            FILE_DEVICE_UNKNOWN,
                            FILE_DEVICE_SECURE_OPEN,
                            FALSE,
                            &Device);
    ok_eq_hex(Status, STATUS_SUCCESS);

    return NT_SUCCESS(Status) ? Device : NULL;
}

static
BOOLEAN
ReadOwnerAndGroup(
    _In_ PDEVICE_OBJECT Device,
    _Out_writes_bytes_(SECURITY_MAX_SID_SIZE) PSID Owner,
    _Out_writes_bytes_(SECURITY_MAX_SID_SIZE) PSID Group,
    _Out_ PULONG AceCount)
{
    PSECURITY_DESCRIPTOR Descriptor = NULL;
    BOOLEAN Allocated = FALSE;
    BOOLEAN Defaulted, Present = FALSE;
    PSID Sid = NULL;
    PACL Dacl = NULL;
    NTSTATUS Status;
    BOOLEAN Complete = TRUE;

    *AceCount = MAXULONG;

    Status = ObGetObjectSecurity(Device, &Descriptor, &Allocated);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status)) return FALSE;

    ok(Descriptor != NULL, "the device has no security descriptor\n");
    if (Descriptor == NULL) return FALSE;

    Status = RtlGetOwnerSecurityDescriptor(Descriptor, &Sid, &Defaulted);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok(Sid != NULL, "the device has no owner\n");
    if (Sid != NULL) RtlCopySid(SECURITY_MAX_SID_SIZE, Owner, Sid);
    else Complete = FALSE;

    Sid = NULL;
    Status = RtlGetGroupSecurityDescriptor(Descriptor, &Sid, &Defaulted);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok(Sid != NULL, "the device has no group\n");
    if (Sid != NULL) RtlCopySid(SECURITY_MAX_SID_SIZE, Group, Sid);
    else Complete = FALSE;

    Status = RtlGetDaclSecurityDescriptor(Descriptor, &Present, &Dacl, &Defaulted);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok(Present && Dacl != NULL, "the device has no DACL\n");
    if (Present && Dacl != NULL) *AceCount = Dacl->AceCount;

    ObReleaseObjectSecurity(Descriptor, Allocated);

    return Complete;
}

static
VOID
SetSystemOnlyDacl(
    _In_ PDEVICE_OBJECT Device)
{
    SECURITY_DESCRIPTOR Descriptor;
    UCHAR AclBuffer[sizeof(ACL) + sizeof(ACCESS_ALLOWED_ACE) + SECURITY_MAX_SID_SIZE];
    PACL Acl = (PACL)AclBuffer;
    HANDLE Handle;
    NTSTATUS Status;

    Status = RtlCreateSecurityDescriptor(&Descriptor, SECURITY_DESCRIPTOR_REVISION);
    ok_eq_hex(Status, STATUS_SUCCESS);
    Status = RtlCreateAcl(Acl, sizeof(AclBuffer), ACL_REVISION);
    ok_eq_hex(Status, STATUS_SUCCESS);
    Status = RtlAddAccessAllowedAce(Acl, ACL_REVISION, FILE_ALL_ACCESS, SeExports->SeLocalSystemSid);
    ok_eq_hex(Status, STATUS_SUCCESS);
    Status = RtlSetDaclSecurityDescriptor(&Descriptor, TRUE, Acl, FALSE);
    ok_eq_hex(Status, STATUS_SUCCESS);

    Status = ObOpenObjectByPointer(Device,
                                   OBJ_KERNEL_HANDLE,
                                   NULL,
                                   READ_CONTROL | WRITE_DAC,
                                   NULL,
                                   KernelMode,
                                   &Handle);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status)) return;

    Status = ZwSetSecurityObject(Handle, DACL_SECURITY_INFORMATION, &Descriptor);
    ok_eq_hex(Status, STATUS_SUCCESS);

    ZwClose(Handle);
}

static
LONG
OutstandingPagedAllocations(VOID)
{
    SYSTEM_PERFORMANCE_INFORMATION Performance;
    NTSTATUS Status;

    Status = ZwQuerySystemInformation(SystemPerformanceInformation, &Performance, sizeof(Performance), NULL);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status)) return 0;

    return (LONG)(Performance.PagedPoolAllocs - Performance.PagedPoolFrees);
}

static
VOID
TestReadAndSet(VOID)
{
    UCHAR Owner[SECURITY_MAX_SID_SIZE], Group[SECURITY_MAX_SID_SIZE];
    UCHAR NewOwner[SECURITY_MAX_SID_SIZE], NewGroup[SECURITY_MAX_SID_SIZE];
    PDEVICE_OBJECT Device;
    ULONG AceCount;

    Device = CreateNamedDevice(L"\\Device\\KmtIoDeviceSecurity");
    if (Device == NULL) return;

    if (ReadOwnerAndGroup(Device, Owner, Group, &AceCount))
    {
        ok(AceCount != 0 && AceCount != MAXULONG, "the named device has %lu entries in its DACL\n", AceCount);

        if (ReadOwnerAndGroup(Device, NewOwner, NewGroup, &AceCount))
        {
            ok(RtlEqualSid(NewOwner, Owner), "reading the security changed the owner\n");
            ok(RtlEqualSid(NewGroup, Group), "reading the security changed the group\n");
        }

        SetSystemOnlyDacl(Device);

        if (ReadOwnerAndGroup(Device, NewOwner, NewGroup, &AceCount))
        {
            ok(RtlEqualSid(NewOwner, Owner), "setting the DACL changed the owner\n");
            ok(RtlEqualSid(NewGroup, Group), "setting the DACL changed the group\n");
        }
        ok_eq_ulong(AceCount, 1UL);
    }

    IoDeleteDevice(Device);
}

static
VOID
TestPoolComesBack(VOID)
{
    PDEVICE_OBJECT Device;
    LONG Before, After;
    ULONG Cycle;

    Before = OutstandingPagedAllocations();

    for (Cycle = 0; Cycle < CYCLES; Cycle++)
    {
        Device = CreateNamedDevice(L"\\Device\\KmtIoDeviceSecurityCycle");
        if (Device == NULL) break;
        IoDeleteDevice(Device);
    }

    After = OutstandingPagedAllocations();

    ok(After - Before < CYCLES / 2,
       "%lu named devices created and deleted kept %ld paged allocations\n", Cycle, After - Before);
}

START_TEST(IoDeviceSecurity)
{
    TestReadAndSet();
    TestPoolComesBack();
}
