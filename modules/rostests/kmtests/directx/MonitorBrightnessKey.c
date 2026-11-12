/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     An ACPI brightness hotkey on an integrated panel reaches the shell even without brightness control
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>
#include <acpiioct.h>
#include <reactos/drivers/acpi/acpipci.h>
#include <reactos/rddm/rxgkmonitor.h>

/* The panel's adapter carries BKEY(Code), which raises Notify(<panel output>, Code); the harness SSDT provides it */
#define KEY_METHOD 'YEKB'
#define KEY_WAIT_MS 500
#define NO_KEY_WAIT_MS 100

static const GUID KmtAcpiPciInterface =
    { 0xA7E9DB84, 0xDB4C, 0x4289, { 0x87, 0x6B, 0xE2, 0xC3, 0xE6, 0x3B, 0x5F, 0x4F } };

typedef struct _KEY_EVAL_INPUT
{
    ACPI_PCI_EVAL_INPUT_BUFFER Header;
    ACPI_EVAL_INPUT_BUFFER_SIMPLE_INTEGER Eval;
} KEY_EVAL_INPUT;

static
NTSTATUS
RaiseKey(
    _In_ PDEVICE_OBJECT AcpiDevice,
    _In_ ULONG Device,
    _In_ ULONG Code)
{
    KEY_EVAL_INPUT Input;
    ACPI_EVAL_OUTPUT_BUFFER Output;
    IO_STATUS_BLOCK IoStatus;
    KEVENT Event;
    NTSTATUS Status;
    PIRP Irp;

    RtlZeroMemory(&Input, sizeof(Input));
    Input.Header.Signature = ACPI_PCI_EVAL_INPUT_BUFFER_SIGNATURE;
    Input.Header.Device = Device;
    Input.Header.TotalSize = sizeof(Input);
    Input.Header.InputBufferOffset = FIELD_OFFSET(KEY_EVAL_INPUT, Eval);
    Input.Header.InputBufferSize = sizeof(Input.Eval);
    Input.Eval.Signature = ACPI_EVAL_INPUT_BUFFER_SIMPLE_INTEGER_SIGNATURE;
    Input.Eval.MethodNameAsUlong = KEY_METHOD;
    Input.Eval.IntegerArgument = Code;

    KeInitializeEvent(&Event, NotificationEvent, FALSE);
    Irp = IoBuildDeviceIoControlRequest(IOCTL_ACPI_EVAL_METHOD_FOR_PCI, AcpiDevice, &Input, sizeof(Input),
                                        &Output, sizeof(Output), FALSE, &Event, &IoStatus);
    if (!Irp)
        return STATUS_INSUFFICIENT_RESOURCES;
    Status = IoCallDriver(AcpiDevice, Irp);
    if (Status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
        Status = IoStatus.Status;
    }
    return Status;
}

static
PDEVICE_OBJECT
OpenAcpiInterface(
    _Out_ PFILE_OBJECT *FileObject)
{
    PDEVICE_OBJECT DeviceObject = NULL;
    UNICODE_STRING Link;
    PWSTR Links = NULL;

    *FileObject = NULL;
    if (!NT_SUCCESS(IoGetDeviceInterfaces(&KmtAcpiPciInterface, NULL, 0, &Links)) || !Links)
        return NULL;
    if (*Links != UNICODE_NULL)
    {
        RtlInitUnicodeString(&Link, Links);
        if (!NT_SUCCESS(IoGetDeviceObjectPointer(&Link, FILE_READ_ATTRIBUTES, FileObject, &DeviceObject)))
            DeviceObject = NULL;
    }
    ExFreePool(Links);
    return DeviceObject;
}

static
NTSTATUS
WaitKey(
    _In_ HANDLE KeyEvent,
    _In_ ULONG Milliseconds)
{
    LARGE_INTEGER Timeout;

    Timeout.QuadPart = -(LONGLONG)Milliseconds * 10000;
    return ZwWaitForSingleObject(KeyEvent, FALSE, &Timeout);
}

static
VOID
TestUsersMayWait(
    _In_ HANDLE KeyEvent)
{
    ULONG Buffer[64];
    PSECURITY_DESCRIPTOR Descriptor = (PSECURITY_DESCRIPTOR)Buffer;
    PACCESS_ALLOWED_ACE Ace;
    BOOLEAN Present, Defaulted, Granted = FALSE;
    ULONG Length, Index;
    NTSTATUS Status;
    PACL Dacl = NULL;

    Status = ZwQuerySecurityObject(KeyEvent, DACL_SECURITY_INFORMATION, Descriptor, sizeof(Buffer), &Length);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        return;
    Status = RtlGetDaclSecurityDescriptor(Descriptor, &Present, &Dacl, &Defaulted);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok(Present && Dacl != NULL, "The brightness key event has no DACL\n");
    for (Index = 0; Dacl && Index < Dacl->AceCount; Index++)
    {
        if (!NT_SUCCESS(RtlGetAce(Dacl, Index, (PVOID *)&Ace)))
            break;
        if (Ace->Header.AceType == ACCESS_ALLOWED_ACE_TYPE && (Ace->Mask & SYNCHRONIZE) &&
            RtlEqualSid(&Ace->SidStart, SeExports->SeWorldSid))
        {
            Granted = TRUE;
        }
    }
    ok(Granted, "Everyone may not wait on the brightness key event\n");
}

START_TEST(MonitorBrightnessKey)
{
    static const ULONG HotkeyCodes[] = { 0x86, 0x87, 0x85, 0x88 };
    UNICODE_STRING Name = RTL_CONSTANT_STRING(L"\\BaseNamedObjects\\" RXGK_MONITOR_BRIGHTNESS_KEY_EVENT);
    OBJECT_ATTRIBUTES Attributes;
    PFILE_OBJECT FileObject;
    PDEVICE_OBJECT AcpiDevice;
    HANDLE KeyEvent;
    ULONG Device, Index;
    NTSTATUS Status;
    LARGE_INTEGER Zero;

    AcpiDevice = OpenAcpiInterface(&FileObject);
    if (skip(AcpiDevice != NULL, "No ACPI PCI interface\n"))
        return;

    InitializeObjectAttributes(&Attributes, &Name, OBJ_KERNEL_HANDLE, NULL, NULL);
    Status = ZwOpenEvent(&KeyEvent, SYNCHRONIZE | READ_CONTROL, &Attributes);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
    {
        ObDereferenceObject(FileObject);
        return;
    }
    TestUsersMayWait(KeyEvent);
    Zero.QuadPart = 0;
    (VOID)ZwWaitForSingleObject(KeyEvent, FALSE, &Zero);

    Status = STATUS_NOT_FOUND;
    for (Device = 0; Device < 32; Device++)
    {
        Status = RaiseKey(AcpiDevice, Device, 0x86);
        if (NT_SUCCESS(Status))
            break;
    }
    if (!skip(NT_SUCCESS(Status), "No adapter carries the BKEY test method\n"))
    {
        Status = WaitKey(KeyEvent, KEY_WAIT_MS);
        ok(Status == STATUS_SUCCESS, "Brightness up on device %lu: wait returned 0x%08lx\n", Device, Status);

        for (Index = 1; Index < RTL_NUMBER_OF(HotkeyCodes); Index++)
        {
            Status = RaiseKey(AcpiDevice, Device, HotkeyCodes[Index]);
            ok_eq_hex(Status, STATUS_SUCCESS);
            Status = WaitKey(KeyEvent, KEY_WAIT_MS);
            ok(Status == STATUS_SUCCESS, "Notify 0x%lx: wait returned 0x%08lx\n", HotkeyCodes[Index], Status);
        }

        Status = RaiseKey(AcpiDevice, Device, 0x80);
        ok_eq_hex(Status, STATUS_SUCCESS);
        Status = WaitKey(KeyEvent, NO_KEY_WAIT_MS);
        ok(Status == STATUS_TIMEOUT, "Notify 0x80 signalled the brightness key: 0x%08lx\n", Status);
    }

    ZwClose(KeyEvent);
    ObDereferenceObject(FileObject);
}
