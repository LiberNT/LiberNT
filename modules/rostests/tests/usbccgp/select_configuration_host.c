/*
 * PROJECT:     LiberNT host-side USBCCGP tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test function select-configuration requests that pick another alternate setting
 * COPYRIGHT:   Copyright 2026 mikuta0407 <mikuta0407@mikuta0407.net>
 */
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define NTAPI
#define TRUE 1
#define FALSE 0
#define NonPagedPool 0
#define DPRINT(...)
#define DPRINT1(...)
#define ASSERT assert
#define C_ASSERT(e)
#define FIELD_OFFSET(t, f) offsetof(t, f)
#define RtlCopyMemory memcpy
#define NT_SUCCESS(s) ((NTSTATUS)(s) >= 0)
#define STATUS_SUCCESS ((NTSTATUS)0)
#define STATUS_UNSUCCESSFUL ((NTSTATUS)0xC0000001)
#define STATUS_INVALID_PARAMETER ((NTSTATUS)0xC000000D)
#define STATUS_INSUFFICIENT_RESOURCES ((NTSTATUS)0xC000009A)
#define STATUS_DEVICE_DATA_ERROR ((NTSTATUS)0xC000009C)
#define URB_FUNCTION_SELECT_INTERFACE 1
typedef int32_t NTSTATUS, LONG, USBD_STATUS;
typedef uint32_t ULONG;
typedef uint16_t USHORT;
typedef uint8_t UCHAR;
typedef uintptr_t ULONG_PTR;
typedef void *PVOID, *USBD_PIPE_HANDLE, *USBD_INTERFACE_HANDLE, *USBD_CONFIGURATION_HANDLE;
typedef int POOL_TYPE, USBD_PIPE_TYPE;
#pragma pack(push, 1)
typedef struct
{
    UCHAR bLength;
    UCHAR bDescriptorType;
    USHORT wTotalLength;
    UCHAR bNumInterfaces;
    UCHAR bConfigurationValue;
    UCHAR iConfiguration;
    UCHAR bmAttributes;
    UCHAR MaxPower;
} USB_CONFIGURATION_DESCRIPTOR, *PUSB_CONFIGURATION_DESCRIPTOR;
typedef struct
{
    UCHAR bLength;
    UCHAR bDescriptorType;
    UCHAR bInterfaceNumber;
    UCHAR bAlternateSetting;
    UCHAR bNumEndpoints;
    UCHAR bInterfaceClass;
    UCHAR bInterfaceSubClass;
    UCHAR bInterfaceProtocol;
    UCHAR iInterface;
} USB_INTERFACE_DESCRIPTOR, *PUSB_INTERFACE_DESCRIPTOR;
#pragma pack(pop)
typedef struct
{
    USHORT MaximumPacketSize;
    UCHAR EndpointAddress;
    UCHAR Interval;
    USBD_PIPE_TYPE PipeType;
    USBD_PIPE_HANDLE PipeHandle;
    ULONG MaximumTransferSize;
    ULONG PipeFlags;
} USBD_PIPE_INFORMATION;
typedef struct
{
    USHORT Length;
    UCHAR InterfaceNumber;
    UCHAR AlternateSetting;
    UCHAR Class;
    UCHAR SubClass;
    UCHAR Protocol;
    UCHAR Reserved;
    USBD_INTERFACE_HANDLE InterfaceHandle;
    ULONG NumberOfPipes;
    USBD_PIPE_INFORMATION Pipes[1];
} USBD_INTERFACE_INFORMATION, *PUSBD_INTERFACE_INFORMATION;
struct _URB_HEADER
{
    USHORT Length;
    USHORT Function;
    USBD_STATUS Status;
    PVOID UsbdDeviceHandle;
    ULONG UsbdFlags;
};
struct _URB_SELECT_INTERFACE
{
    struct _URB_HEADER Hdr;
    USBD_CONFIGURATION_HANDLE ConfigurationHandle;
    USBD_INTERFACE_INFORMATION Interface;
};
struct _URB_SELECT_CONFIGURATION
{
    struct _URB_HEADER Hdr;
    PUSB_CONFIGURATION_DESCRIPTOR ConfigurationDescriptor;
    USBD_CONFIGURATION_HANDLE ConfigurationHandle;
    USBD_INTERFACE_INFORMATION Interface;
};
typedef union
{
    struct _URB_HEADER UrbHeader;
    struct _URB_SELECT_INTERFACE UrbSelectInterface;
    struct _URB_SELECT_CONFIGURATION UrbSelectConfiguration;
} URB, *PURB;
#define GET_SELECT_INTERFACE_REQUEST_SIZE(totalPipes) \
    (sizeof(struct _URB_SELECT_INTERFACE) + ((totalPipes) - 1) * sizeof(USBD_PIPE_INFORMATION))
#define GET_USBD_INTERFACE_SIZE(numEndpoints) \
    (sizeof(USBD_INTERFACE_INFORMATION) + (sizeof(USBD_PIPE_INFORMATION) * (numEndpoints)) - sizeof(USBD_PIPE_INFORMATION))
#define UsbBuildSelectInterfaceRequest(urb, length, configurationHandle, interfaceNumber, alternateSetting) \
    do                                                                                                     \
    {                                                                                                      \
        (urb)->UrbHeader.Function = URB_FUNCTION_SELECT_INTERFACE;                                         \
        (urb)->UrbHeader.Length = (length);                                                                \
        (urb)->UrbSelectInterface.Interface.AlternateSetting = (alternateSetting);                         \
        (urb)->UrbSelectInterface.Interface.InterfaceNumber = (interfaceNumber);                           \
        (urb)->UrbSelectInterface.ConfigurationHandle = (configurationHandle);                             \
    } while (0)
typedef struct
{
    USB_INTERFACE_DESCRIPTOR *InterfaceDescriptor;
    PUSBD_INTERFACE_INFORMATION Interface;
} USBD_INTERFACE_LIST_ENTRY, *PUSBD_INTERFACE_LIST_ENTRY;
typedef struct
{
    UCHAR FunctionNumber;
    UCHAR NumberOfInterfaces;
    PUSB_INTERFACE_DESCRIPTOR *InterfaceDescriptorList;
} USBC_FUNCTION_DESCRIPTOR, *PUSBC_FUNCTION_DESCRIPTOR;
typedef struct
{
    PVOID DeviceExtension;
} DEVICE_OBJECT, *PDEVICE_OBJECT;
typedef struct
{
    struct
    {
        struct
        {
            PVOID Argument1;
        } Others;
    } Parameters;
} IO_STACK_LOCATION, *PIO_STACK_LOCATION;
typedef struct
{
    IO_STACK_LOCATION Stack;
} IRP, *PIRP;
typedef struct
{
    PUSBC_FUNCTION_DESCRIPTOR FunctionDescriptor;
    PDEVICE_OBJECT NextDeviceObject;
    PUSB_CONFIGURATION_DESCRIPTOR ConfigurationDescriptor;
    USBD_CONFIGURATION_HANDLE ConfigurationHandle;
    PUSBD_INTERFACE_LIST_ENTRY InterfaceList;
    ULONG InterfaceListCount;
} PDO_DEVICE_EXTENSION, *PPDO_DEVICE_EXTENSION;

static PIO_STACK_LOCATION IoGetCurrentIrpStackLocation(PIRP Irp)
{
    return &Irp->Stack;
}

static int live_items;
static int fail_allocation_at = -1;
static PVOID AllocateItem(POOL_TYPE PoolType, ULONG ItemSize)
{
    (void)PoolType;
    if (fail_allocation_at == 0)
    {
        fail_allocation_at = -1;
        return NULL;
    }
    if (fail_allocation_at > 0)
        fail_allocation_at--;
    live_items++;
    return calloc(1, ItemSize);
}
static void FreeItem(PVOID Item)
{
    assert(Item);
    live_items--;
    free(Item);
}

static PUSB_INTERFACE_DESCRIPTOR
USBD_ParseConfigurationDescriptorEx(PUSB_CONFIGURATION_DESCRIPTOR Configuration, PVOID Start, LONG InterfaceNumber,
                                    LONG AlternateSetting, LONG Class, LONG SubClass, LONG Protocol)
{
    UCHAR *p = Start, *end = (UCHAR *)Configuration + Configuration->wTotalLength;
    (void)Class;
    (void)SubClass;
    (void)Protocol;
    while (p + 2 <= end && p[0] != 0)
    {
        PUSB_INTERFACE_DESCRIPTOR d = (PUSB_INTERFACE_DESCRIPTOR)p;
        if (p[1] == 4 && (InterfaceNumber == -1 || d->bInterfaceNumber == InterfaceNumber) &&
            (AlternateSetting == -1 || d->bAlternateSetting == AlternateSetting))
            return d;
        p += p[0];
    }
    return NULL;
}

static UCHAR config[] = {
    9, 2, 9 + 9 + 7 + 9 + 9 + 7 + 7, 0, 2, 1, 0, 0x80, 50,
    9, 4, 0, 0, 1, 2, 0x0d, 0, 0,
    7, 5, 0x81, 3, 16, 0, 9,
    9, 4, 1, 0, 0, 0x0a, 0, 1, 0,
    9, 4, 1, 1, 2, 0x0a, 0, 1, 0,
    7, 5, 0x82, 2, 0, 2, 0,
    7, 5, 0x03, 2, 0, 2, 0,
};
#define DATA_ALT0 ((PUSB_INTERFACE_DESCRIPTOR)(config + 9 + 9 + 7))
#define DATA_ALT1 ((PUSB_INTERFACE_DESCRIPTOR)(config + 9 + 9 + 7 + 9))

static int lower_calls;
static NTSTATUS lower_status;
static USHORT lower_seen_length;
static ULONG lower_seen_transfer[2];
static USBD_CONFIGURATION_HANDLE config_handle = (USBD_CONFIGURATION_HANDLE)0x1000;

/* Lower stack: the checks USBPORT applies to URB_FUNCTION_SELECT_INTERFACE. */
static NTSTATUS USBCCGP_SyncUrbRequest(PDEVICE_OBJECT DeviceObject, PURB Urb)
{
    PUSBD_INTERFACE_INFORMATION i = &Urb->UrbSelectInterface.Interface;
    PUSB_INTERFACE_DESCRIPTOR d;
    USHORT need;
    (void)DeviceObject;
    lower_calls++;
    assert(Urb->UrbHeader.Function == URB_FUNCTION_SELECT_INTERFACE);
    assert(Urb->UrbSelectInterface.ConfigurationHandle == config_handle);
    lower_seen_length = i->Length;
    if (!NT_SUCCESS(lower_status))
        return lower_status;
    d = USBD_ParseConfigurationDescriptorEx((PUSB_CONFIGURATION_DESCRIPTOR)config, config, i->InterfaceNumber,
                                            i->AlternateSetting, -1, -1, -1);
    if (!d)
        return STATUS_DEVICE_DATA_ERROR;
    need = (USHORT)GET_USBD_INTERFACE_SIZE(d->bNumEndpoints);
    assert(Urb->UrbHeader.Length >= GET_SELECT_INTERFACE_REQUEST_SIZE(d->bNumEndpoints));
    if (i->Length < need)
        return STATUS_INVALID_PARAMETER;
    for (ULONG p = 0; p < d->bNumEndpoints && p < 2; p++)
        lower_seen_transfer[p] = i->Pipes[p].MaximumTransferSize;
    i->Length = need;
    i->Class = d->bInterfaceClass;
    i->NumberOfPipes = d->bNumEndpoints;
    i->InterfaceHandle = (USBD_INTERFACE_HANDLE)(ULONG_PTR)(0x2000 + d->bInterfaceNumber * 16 + d->bAlternateSetting);
    for (ULONG p = 0; p < d->bNumEndpoints; p++)
    {
        UCHAR *ep = (UCHAR *)d + 9 + 7 * p;
        i->Pipes[p].EndpointAddress = ep[2];
        i->Pipes[p].MaximumPacketSize = (USHORT)(ep[4] | (ep[5] << 8));
        i->Pipes[p].PipeHandle = (USBD_PIPE_HANDLE)(ULONG_PTR)(0x3000 + ep[2]);
    }
    return STATUS_SUCCESS;
}

#include "select_configuration.h"

static USBC_FUNCTION_DESCRIPTOR function;
static PUSB_INTERFACE_DESCRIPTOR function_interfaces[2];
static USBD_INTERFACE_LIST_ENTRY list[3];
static PDO_DEVICE_EXTENSION pdo;
static DEVICE_OBJECT pdo_object = {&pdo};
static DEVICE_OBJECT lower_object;

static PUSBD_INTERFACE_INFORMATION selected(PUSB_INTERFACE_DESCRIPTOR d)
{
    PUSBD_INTERFACE_INFORMATION i = AllocateItem(NonPagedPool, GET_USBD_INTERFACE_SIZE(d->bNumEndpoints));
    if (!i)
        abort();
    i->Length = (USHORT)GET_USBD_INTERFACE_SIZE(d->bNumEndpoints);
    i->InterfaceNumber = d->bInterfaceNumber;
    i->AlternateSetting = d->bAlternateSetting;
    i->NumberOfPipes = d->bNumEndpoints;
    i->InterfaceHandle = (USBD_INTERFACE_HANDLE)(ULONG_PTR)(0x2000 + d->bInterfaceNumber * 16 + d->bAlternateSetting);
    return i;
}

static void setup(void)
{
    memset(list, 0, sizeof(list));
    list[0].InterfaceDescriptor = (PUSB_INTERFACE_DESCRIPTOR)(config + 9);
    list[0].Interface = selected(list[0].InterfaceDescriptor);
    list[0].Interface->Pipes[0].MaximumTransferSize = 4096;
    list[1].InterfaceDescriptor = DATA_ALT0;
    list[1].Interface = selected(DATA_ALT0);
    function_interfaces[0] = list[0].InterfaceDescriptor;
    function_interfaces[1] = DATA_ALT0;
    function.FunctionNumber = 0;
    function.NumberOfInterfaces = 2;
    function.InterfaceDescriptorList = function_interfaces;
    pdo.FunctionDescriptor = &function;
    pdo.NextDeviceObject = &lower_object;
    pdo.ConfigurationDescriptor = (PUSB_CONFIGURATION_DESCRIPTOR)config;
    pdo.ConfigurationHandle = config_handle;
    pdo.InterfaceList = list;
    pdo.InterfaceListCount = 2;
    lower_calls = 0;
    lower_status = STATUS_SUCCESS;
    lower_seen_length = 0;
    memset(lower_seen_transfer, 0, sizeof(lower_seen_transfer));
}

static void teardown(void)
{
    FreeItem(list[0].Interface);
    FreeItem(list[1].Interface);
    assert(live_items == 0);
}

static PURB request(UCHAR data_alt, UCHAR data_pipes, USHORT data_length)
{
    size_t comm = GET_USBD_INTERFACE_SIZE(1);
    size_t size = offsetof(struct _URB_SELECT_CONFIGURATION, Interface) + comm + data_length;
    PURB urb = calloc(1, size);
    PUSBD_INTERFACE_INFORMATION i;
    if (!urb)
        abort();
    i = &urb->UrbSelectConfiguration.Interface;
    urb->UrbHeader.Length = (USHORT)size;
    urb->UrbSelectConfiguration.ConfigurationDescriptor = (PUSB_CONFIGURATION_DESCRIPTOR)config;
    i->Length = (USHORT)comm;
    i->InterfaceNumber = 0;
    i->NumberOfPipes = 1;
    i->Pipes[0].MaximumTransferSize = 4096;
    i = (PUSBD_INTERFACE_INFORMATION)((UCHAR *)i + comm);
    i->Length = data_length;
    i->InterfaceNumber = 1;
    i->AlternateSetting = data_alt;
    i->NumberOfPipes = data_pipes;
    for (ULONG p = 0; p < data_pipes && offsetof(USBD_INTERFACE_INFORMATION, Pipes[p + 1]) <= data_length; p++)
        i->Pipes[p].MaximumTransferSize = 0x10000 + p;
    return urb;
}

static PUSBD_INTERFACE_INFORMATION data_of(PURB urb)
{
    PUSBD_INTERFACE_INFORMATION i = &urb->UrbSelectConfiguration.Interface;
    return (PUSBD_INTERFACE_INFORMATION)((UCHAR *)i + i->Length);
}

static NTSTATUS submit(PURB urb)
{
    IRP irp;
    irp.Stack.Parameters.Others.Argument1 = urb;
    return USBCCGP_PDOSelectConfiguration(&pdo_object, &irp);
}

static void expect_status(PURB urb, NTSTATUS expected)
{
    NTSTATUS status = submit(urb);
    if (status != expected)
    {
        fprintf(stderr, "status 0x%08x, expected 0x%08x\n", (unsigned)status, (unsigned)expected);
        abort();
    }
}

static void expect_alt1(PURB urb)
{
    PUSBD_INTERFACE_INFORMATION d = data_of(urb);
    assert(urb->UrbSelectConfiguration.ConfigurationHandle == config_handle);
    assert(d->Length == GET_USBD_INTERFACE_SIZE(2) && d->AlternateSetting == 1 && d->NumberOfPipes == 2);
    assert(d->InterfaceHandle == (USBD_INTERFACE_HANDLE)(ULONG_PTR)0x2011);
    assert(d->Pipes[0].EndpointAddress == 0x82 && d->Pipes[1].EndpointAddress == 0x03);
    assert(d->Pipes[0].PipeHandle == (USBD_PIPE_HANDLE)(ULONG_PTR)0x3082);
    assert(d->Pipes[0].MaximumPacketSize == 512);
    assert(list[1].InterfaceDescriptor == DATA_ALT1);
    assert(list[1].Interface->Length == GET_USBD_INTERFACE_SIZE(2) && list[1].Interface->NumberOfPipes == 2);
    assert(list[1].Interface->Pipes[1].PipeHandle == (USBD_PIPE_HANDLE)(ULONG_PTR)0x3003);
}

static void select_alternate_with_more_pipes(void)
{
    PURB urb;
    setup();
    urb = request(1, 2, (USHORT)GET_USBD_INTERFACE_SIZE(2));
    expect_status(urb, STATUS_SUCCESS);
    assert(lower_calls == 1 && lower_seen_length == GET_USBD_INTERFACE_SIZE(2));
    assert(lower_seen_transfer[0] == 0x10000 && lower_seen_transfer[1] == 0x10001);
    expect_alt1(urb);
    free(urb);

    urb = request(1, 2, (USHORT)GET_USBD_INTERFACE_SIZE(2));
    expect_status(urb, STATUS_SUCCESS);
    assert(lower_calls == 1);
    expect_alt1(urb);
    free(urb);

    urb = request(0, 0, (USHORT)GET_USBD_INTERFACE_SIZE(0));
    expect_status(urb, STATUS_SUCCESS);
    assert(lower_calls == 2 && lower_seen_length == GET_USBD_INTERFACE_SIZE(0));
    assert(data_of(urb)->NumberOfPipes == 0 && data_of(urb)->Length == GET_USBD_INTERFACE_SIZE(0));
    assert(list[1].InterfaceDescriptor == DATA_ALT0 && list[1].Interface->Length == GET_USBD_INTERFACE_SIZE(0));
    free(urb);
    teardown();
}

static void unchanged(PUSBD_INTERFACE_INFORMATION before)
{
    assert(list[1].Interface == before && list[1].InterfaceDescriptor == DATA_ALT0);
    assert(before->Length == GET_USBD_INTERFACE_SIZE(0) && before->AlternateSetting == 0);
}

static void reject_bad_requests(void)
{
    PUSBD_INTERFACE_INFORMATION before;
    PURB urb;

    setup();
    before = list[1].Interface;
    urb = request(5, 2, (USHORT)GET_USBD_INTERFACE_SIZE(2));
    expect_status(urb, STATUS_INVALID_PARAMETER);
    assert(lower_calls == 0);
    unchanged(before);
    free(urb);

    urb = request(1, 2, (USHORT)GET_USBD_INTERFACE_SIZE(1));
    expect_status(urb, STATUS_INVALID_PARAMETER);
    assert(lower_calls == 0);
    unchanged(before);
    free(urb);

    lower_status = STATUS_DEVICE_DATA_ERROR;
    urb = request(1, 2, (USHORT)GET_USBD_INTERFACE_SIZE(2));
    expect_status(urb, STATUS_DEVICE_DATA_ERROR);
    assert(lower_calls == 1);
    unchanged(before);
    free(urb);

    lower_status = STATUS_SUCCESS;
    lower_calls = 0;
    fail_allocation_at = 1;
    urb = request(1, 2, (USHORT)GET_USBD_INTERFACE_SIZE(2));
    expect_status(urb, STATUS_INSUFFICIENT_RESOURCES);
    assert(lower_calls == 0);
    unchanged(before);
    free(urb);
    teardown();
}

int main(void)
{
    select_alternate_with_more_pipes();
    reject_bad_requests();
    printf("usbccgp select configuration: 2 groups passed\n");
    return 0;
}
