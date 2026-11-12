/*
 * PROJECT:         LiberNT kernel-mode tests
 * LICENSE:         GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:         Kernel-Mode Test Suite ETW provider registration API
 */

#include <kmt_test.h>
#include <evntrace.h>

#define NDEBUG
#include <debug.h>

static const GUID KmtEtwProviderGuid = { 0x9f5ebcd1, 0x6c2f, 0x4f93, { 0xb1, 0x07, 0x42, 0x88, 0x1f, 0x3a, 0x5e, 0x21 } };

#define KMT_ETW_REGISTRATIONS 1024
#define KMT_ETW_TAG 'wtEK'

static
NTSTATUS
KmtEtwWriteEx(
    _In_ REGHANDLE RegHandle,
    _In_ PCEVENT_DESCRIPTOR EventDescriptor,
    _In_ ULONG Flags,
    _In_ ULONG UserDataCount,
    _In_reads_opt_(UserDataCount) PEVENT_DATA_DESCRIPTOR UserData)
{
    NTSTATUS Status;

    _SEH2_TRY
    {
        Status = EtwWriteEx(RegHandle, EventDescriptor, 0, Flags, NULL, NULL, UserDataCount, UserData);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    return Status;
}

static
NTSTATUS
KmtEtwWriteString(
    _In_ REGHANDLE RegHandle,
    _In_ PCWSTR String)
{
    NTSTATUS Status;

    _SEH2_TRY
    {
        Status = EtwWriteString(RegHandle, TRACE_LEVEL_INFORMATION, 0, NULL, String);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    return Status;
}

static
VOID
TestRegistrationCapacity(
    _In_ PCEVENT_DESCRIPTOR EventDescriptor)
{
    PREGHANDLE Handles;
    GUID Guid = KmtEtwProviderGuid;
    ULONG Registered = 0;
    ULONG i;
    NTSTATUS Status = STATUS_SUCCESS;

    Handles = ExAllocatePoolZero(NonPagedPoolNx, KMT_ETW_REGISTRATIONS * sizeof(REGHANDLE), KMT_ETW_TAG);
    if (skip(Handles != NULL, "out of memory\n"))
        return;

    for (i = 0; i < KMT_ETW_REGISTRATIONS; i++)
    {
        Guid.Data1 = KmtEtwProviderGuid.Data1 + i + 1;
        Status = EtwRegister(&Guid, NULL, NULL, &Handles[i]);
        if (!NT_SUCCESS(Status))
            break;
        Registered++;
    }
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_ulong(Registered, (ULONG)KMT_ETW_REGISTRATIONS);

    if (Registered == KMT_ETW_REGISTRATIONS)
    {
        ok(Handles[0] != Handles[KMT_ETW_REGISTRATIONS - 1], "first and last registrations share a handle\n");
        Status = EtwWrite(Handles[KMT_ETW_REGISTRATIONS - 1], EventDescriptor, NULL, 0, NULL);
        ok_eq_hex(Status, STATUS_SUCCESS);
    }

    Status = STATUS_SUCCESS;
    for (i = 0; i < Registered; i++)
    {
        Status = EtwUnregister(Handles[i]);
        if (!NT_SUCCESS(Status))
            break;
    }
    ok_eq_hex(Status, STATUS_SUCCESS);

    ExFreePoolWithTag(Handles, KMT_ETW_TAG);
}

START_TEST(EtwRegisterKM)
{
    REGHANDLE RegHandle = 0;
    EVENT_DESCRIPTOR EventDescriptor;
    EVENT_DATA_DESCRIPTOR Data;
    ULONG Value = 0x1234;
    NTSTATUS Status;

    Status = EtwRegister(&KmtEtwProviderGuid, NULL, NULL, &RegHandle);
    if (Status == STATUS_NOT_IMPLEMENTED)
    {
        skip(FALSE, "ETW provider registration not available in this context\n");
        return;
    }
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status)) return;
    ok(RegHandle != 0, "reg handle zero\n");

    RtlZeroMemory(&EventDescriptor, sizeof(EventDescriptor));
    EventDescriptor.Id = 1;
    EventDescriptor.Version = 0;
    EventDescriptor.Level = 4;

    Status = EtwWrite(RegHandle, &EventDescriptor, NULL, 0, NULL);
    ok_eq_hex(Status, STATUS_SUCCESS);

    /* A handle that names no registration, whether or not anybody listens */
    Status = EtwWrite(0, &EventDescriptor, NULL, 0, NULL);
    ok_eq_hex(Status, STATUS_INVALID_HANDLE);
    Status = EtwWriteTransfer(0, &EventDescriptor, NULL, NULL, 0, NULL);
    ok_eq_hex(Status, STATUS_INVALID_HANDLE);

    EventDataDescCreate(&Data, &Value, sizeof(Value));

    Status = KmtEtwWriteEx(RegHandle, &EventDescriptor, 0, 0, NULL);
    ok_eq_hex(Status, STATUS_SUCCESS);
    Status = KmtEtwWriteEx(RegHandle, &EventDescriptor, EVENT_WRITE_FLAG_NO_FAULTING, 1, &Data);
    ok_eq_hex(Status, STATUS_SUCCESS);
    Status = KmtEtwWriteEx(0, &EventDescriptor, 0, 1, &Data);
    ok_eq_hex(Status, STATUS_INVALID_HANDLE);

    Status = KmtEtwWriteString(RegHandle, L"kmtest");
    ok_eq_hex(Status, STATUS_SUCCESS);
    Status = KmtEtwWriteString(0, L"kmtest");
    ok_eq_hex(Status, STATUS_INVALID_HANDLE);

    Status = EtwUnregister(RegHandle);
    ok_eq_hex(Status, STATUS_SUCCESS);

    /* A registration that has gone */
    Status = EtwWrite(RegHandle, &EventDescriptor, NULL, 0, NULL);
    ok_eq_hex(Status, STATUS_INVALID_HANDLE);

    TestRegistrationCapacity(&EventDescriptor);
}
