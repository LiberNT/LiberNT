/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     WSK name resolution through NETIO
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>
#include <ndis.h>
#include <wsk.h>

#define NDEBUG
#include <debug.h>

#include "wskname.h"

static KMT_MESSAGE_HANDLER TestMessageHandler;

static WSK_REGISTRATION Registration;
static WSK_PROVIDER_NPI Provider;
static BOOLEAN Registered;
static BOOLEAN Captured;

static
NTSTATUS
NTAPI
TestComplete(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp,
    _In_ PVOID Context)
{
    UNREFERENCED_PARAMETER(DeviceObject);
    UNREFERENCED_PARAMETER(Irp);

    KeSetEvent(Context, IO_NO_INCREMENT, FALSE);
    return STATUS_MORE_PROCESSING_REQUIRED;
}

static
NTSTATUS
TestWait(
    _In_ PIRP Irp,
    _In_ PKEVENT Event,
    _In_ NTSTATUS Status)
{
    if (Status == STATUS_PENDING)
    {
        KeWaitForSingleObject(Event, Executive, KernelMode, FALSE, NULL);
        Status = Irp->IoStatus.Status;
    }
    return Status;
}

static
PIRP
TestBuildIrp(
    _In_ PKEVENT Event)
{
    PIRP Irp = IoAllocateIrp(1, FALSE);

    if (Irp == NULL)
        return NULL;

    KeInitializeEvent(Event, NotificationEvent, FALSE);
    IoSetCompletionRoutine(Irp, TestComplete, Event, TRUE, TRUE, TRUE);
    return Irp;
}

/* A literal address needs no resolver, so it answers the same with the service stopped. */
static
VOID
TestNumericAddress(VOID)
{
    UNICODE_STRING Node = RTL_CONSTANT_STRING(L"10.1.2.3");
    UNICODE_STRING Service = RTL_CONSTANT_STRING(L"445");
    PADDRINFOEXW Result = NULL;
    ADDRINFOEXW Hints;
    KEVENT Event;
    PIRP Irp;
    NTSTATUS Status;

    Irp = TestBuildIrp(&Event);
    if (!skip(Irp != NULL, "no IRP\n"))
    {
        RtlZeroMemory(&Hints, sizeof(Hints));
        Hints.ai_family = AF_INET;
        Status = Provider.Dispatch->WskGetAddressInfo(Provider.Client, &Node, &Service, 0, NULL, &Hints, &Result,
                                                      NULL, NULL, Irp);
        Status = TestWait(Irp, &Event, Status);
        ok_eq_hex(Status, STATUS_SUCCESS);
        if (NT_SUCCESS(Status) && !skip(Result != NULL, "no result\n"))
        {
            PSOCKADDR_IN Address = (PSOCKADDR_IN)Result->ai_addr;

            ok_eq_int(Result->ai_family, AF_INET);
            ok_eq_ulong((ULONG)Result->ai_addrlen, (ULONG)sizeof(SOCKADDR_IN));
            ok_eq_int(Address->sin_family, AF_INET);
            ok_eq_ulong(RtlUshortByteSwap(Address->sin_port), 445UL);
            ok_eq_ulong(RtlUlongByteSwap(Address->sin_addr.s_addr), 0x0A010203UL);
            ok(Result->ai_next == NULL, "A literal address returned more than one entry\n");
            Provider.Dispatch->WskFreeAddressInfo(Provider.Client, Result);
        }
        IoFreeIrp(Irp);
    }
}

/* AI_NUMERICHOST forbids a lookup, so a name must fail instead of reaching the resolver. */
static
VOID
TestNumericOnly(VOID)
{
    UNICODE_STRING Node = RTL_CONSTANT_STRING(L"libernt.invalid");
    PADDRINFOEXW Result = NULL;
    ADDRINFOEXW Hints;
    KEVENT Event;
    PIRP Irp;
    NTSTATUS Status;

    Irp = TestBuildIrp(&Event);
    if (!skip(Irp != NULL, "no IRP\n"))
    {
        RtlZeroMemory(&Hints, sizeof(Hints));
        Hints.ai_flags = AI_NUMERICHOST;
        Status = Provider.Dispatch->WskGetAddressInfo(Provider.Client, &Node, NULL, 0, NULL, &Hints, &Result,
                                                      NULL, NULL, Irp);
        Status = TestWait(Irp, &Event, Status);
        ok_eq_hex(Status, STATUS_INVALID_PARAMETER);
        ok(Result == NULL, "A refused lookup returned a result\n");
        IoFreeIrp(Irp);
    }
}

static
VOID
TestBadArguments(VOID)
{
    PADDRINFOEXW Result = NULL;
    KEVENT Event;
    PIRP Irp;
    NTSTATUS Status;

    Irp = TestBuildIrp(&Event);
    if (!skip(Irp != NULL, "no IRP\n"))
    {
        Status = Provider.Dispatch->WskGetAddressInfo(Provider.Client, NULL, NULL, 0, NULL, NULL, &Result,
                                                      NULL, NULL, Irp);
        Status = TestWait(Irp, &Event, Status);
        ok_eq_hex(Status, STATUS_INVALID_PARAMETER);
        IoFreeIrp(Irp);
    }
}

/* NI_NUMERICHOST is answered without the resolver too. */
static
VOID
TestNumericName(VOID)
{
    WCHAR NodeBuffer[64] = L"";
    WCHAR ServiceBuffer[16] = L"";
    UNICODE_STRING NodeName, ServiceName;
    SOCKADDR_IN Address;
    KEVENT Event;
    PIRP Irp;
    NTSTATUS Status;

    RtlZeroMemory(&Address, sizeof(Address));
    Address.sin_family = AF_INET;
    Address.sin_port = RtlUshortByteSwap(8080);
    Address.sin_addr.s_addr = RtlUlongByteSwap(0xC0A80105);

    RtlInitEmptyUnicodeString(&NodeName, NodeBuffer, sizeof(NodeBuffer));
    RtlInitEmptyUnicodeString(&ServiceName, ServiceBuffer, sizeof(ServiceBuffer));

    Irp = TestBuildIrp(&Event);
    if (!skip(Irp != NULL, "no IRP\n"))
    {
        Status = Provider.Dispatch->WskGetNameInfo(Provider.Client, (PSOCKADDR)&Address, sizeof(Address), &NodeName,
                                                   &ServiceName, NI_NUMERICHOST | NI_NUMERICSERV, NULL, NULL, Irp);
        Status = TestWait(Irp, &Event, Status);
        ok_eq_hex(Status, STATUS_SUCCESS);
        if (NT_SUCCESS(Status))
        {
            ok(wcscmp(NodeBuffer, L"192.168.1.5") == 0, "Node is '%S'\n", NodeBuffer);
            ok(wcscmp(ServiceBuffer, L"8080") == 0, "Service is '%S'\n", ServiceBuffer);
        }
        IoFreeIrp(Irp);
    }
}

static
VOID
TestShortAddress(VOID)
{
    SOCKADDR_IN Address;
    WCHAR NodeBuffer[64] = L"";
    UNICODE_STRING NodeName;
    KEVENT Event;
    PIRP Irp;
    NTSTATUS Status;

    RtlZeroMemory(&Address, sizeof(Address));
    Address.sin_family = AF_INET;
    RtlInitEmptyUnicodeString(&NodeName, NodeBuffer, sizeof(NodeBuffer));

    Irp = TestBuildIrp(&Event);
    if (!skip(Irp != NULL, "no IRP\n"))
    {
        Status = Provider.Dispatch->WskGetNameInfo(Provider.Client, (PSOCKADDR)&Address, sizeof(Address) - 1,
                                                   &NodeName, NULL, NI_NUMERICHOST, NULL, NULL, Irp);
        Status = TestWait(Irp, &Event, Status);
        ok_eq_hex(Status, STATUS_INVALID_PARAMETER);
        IoFreeIrp(Irp);
    }
}

/* The whole chain: NETIO hands the name to the DNS Client service, which resolves it. */
static
VOID
TestResolver(VOID)
{
    UNICODE_STRING Node = RTL_CONSTANT_STRING(WSKNAME_TEST_NODE);
    UNICODE_STRING Service = RTL_CONSTANT_STRING(L"domain");
    PADDRINFOEXW Result = NULL;
    ADDRINFOEXW Hints;
    KEVENT Event;
    PIRP Irp;
    NTSTATUS Status;

    Irp = TestBuildIrp(&Event);
    if (skip(Irp != NULL, "no IRP\n"))
        return;

    RtlZeroMemory(&Hints, sizeof(Hints));
    Hints.ai_family = AF_INET;
    Status = Provider.Dispatch->WskGetAddressInfo(Provider.Client, &Node, &Service, 0, NULL, &Hints, &Result,
                                                  NULL, NULL, Irp);
    Status = TestWait(Irp, &Event, Status);
    if (Status == STATUS_NO_MATCH)
    {
        trace("No name resolver is listening, the service part is untested\n");
        IoFreeIrp(Irp);
        return;
    }

    ok_eq_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status) && !skip(Result != NULL, "no result\n"))
    {
        PSOCKADDR_IN Address = (PSOCKADDR_IN)Result->ai_addr;

        ok_eq_int(Result->ai_family, AF_INET);
        ok_eq_ulong(RtlUlongByteSwap(Address->sin_addr.s_addr), (ULONG)WSKNAME_TEST_ADDRESS);
        ok_eq_ulong(RtlUshortByteSwap(Address->sin_port), (ULONG)WSKNAME_TEST_PORT);
        Provider.Dispatch->WskFreeAddressInfo(Provider.Client, Result);
    }
    IoFreeIrp(Irp);
}

static
VOID
TestRun(VOID)
{
    static const WSK_CLIENT_DISPATCH Dispatch = { MAKE_WSK_VERSION(1, 0), 0, NULL };
    WSK_CLIENT_NPI Npi;
    NTSTATUS Status;

    Npi.ClientContext = NULL;
    Npi.Dispatch = &Dispatch;

    Status = WskRegister(&Npi, &Registration);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        return;
    Registered = TRUE;

    Status = WskCaptureProviderNPI(&Registration, WSK_INFINITE_WAIT, &Provider);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        return;
    Captured = TRUE;

    if (skip(Provider.Dispatch->WskGetAddressInfo != NULL && Provider.Dispatch->WskFreeAddressInfo != NULL &&
                 Provider.Dispatch->WskGetNameInfo != NULL,
             "The provider has no name resolution\n"))
    {
        return;
    }

    TestNumericAddress();
    TestNumericOnly();
    TestBadArguments();
    TestNumericName();
    TestShortAddress();
    TestResolver();
}

static
NTSTATUS
TestMessageHandler(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ ULONG ControlCode,
    _In_opt_ PVOID Buffer,
    _In_ SIZE_T InLength,
    _Inout_ PSIZE_T OutLength)
{
    UNREFERENCED_PARAMETER(DeviceObject);
    UNREFERENCED_PARAMETER(Buffer);
    UNREFERENCED_PARAMETER(InLength);
    UNREFERENCED_PARAMETER(OutLength);

    if (ControlCode == IOCTL_WSKNAME_RUN)
        TestRun();

    return STATUS_SUCCESS;
}

NTSTATUS
TestEntry(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PCUNICODE_STRING RegistryPath,
    _Out_ PCWSTR *DeviceName,
    _Inout_ INT *Flags)
{
    UNREFERENCED_PARAMETER(DriverObject);
    UNREFERENCED_PARAMETER(RegistryPath);

    *DeviceName = L"WskName";
    *Flags = TESTENTRY_NO_EXCLUSIVE_DEVICE | TESTENTRY_BUFFERED_IO_DEVICE | TESTENTRY_NO_READONLY_DEVICE;

    KmtRegisterMessageHandler(0, NULL, TestMessageHandler);
    return STATUS_SUCCESS;
}

VOID
TestUnload(
    _In_ PDRIVER_OBJECT DriverObject)
{
    UNREFERENCED_PARAMETER(DriverObject);

    if (Captured)
        WskReleaseProviderNPI(&Registration);
    if (Registered)
        WskDeregister(&Registration);
}
