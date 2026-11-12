/*
 * PROJECT:     LiberNT WinUSB
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     WinUSB API on top of the LiberNT winusb.sys driver
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <stdarg.h>

#define WIN32_NO_STATUS
#include <windef.h>
#include <winbase.h>
#include <winioctl.h>
#include <winternl.h>
#undef WIN32_NO_STATUS
#include <ntstatus.h>
#include <winusb.h>
#include <reactos/drivers/lntwinusb.h>

#define WINUSB_HANDLE_MAGIC 0x42535557
#define WINUSB_INFORMATION_SIZE (FIELD_OFFSET(LNT_WINUSB_INFORMATION, Configuration) + 0x10000)

typedef struct _WINUSB_ROOT
{
    LONG References;
    HANDLE File;
    CRITICAL_SECTION Lock;
    ULONG Claimed;
    PLNT_WINUSB_INFORMATION Information;
} WINUSB_ROOT, *PWINUSB_ROOT;

typedef struct _WINUSB_HANDLE
{
    ULONG Magic;
    PWINUSB_ROOT Root;
    UCHAR InterfaceIndex;
} WINUSB_HANDLE, *PWINUSB_HANDLE;

static
PWINUSB_HANDLE
WinUsbHandle(
    _In_opt_ WINUSB_INTERFACE_HANDLE InterfaceHandle)
{
    PWINUSB_HANDLE Handle = InterfaceHandle;

    if (!Handle || Handle->Magic != WINUSB_HANDLE_MAGIC)
    {
        SetLastError(ERROR_INVALID_HANDLE);
        return NULL;
    }
    return Handle;
}

static
BOOL
WinUsbIoctl(
    _In_ HANDLE File,
    _In_ DWORD Code,
    _In_reads_bytes_opt_(InputLength) PVOID Input,
    _In_ DWORD InputLength,
    _Out_writes_bytes_opt_(OutputLength) PVOID Output,
    _In_ DWORD OutputLength,
    _Out_opt_ PDWORD Returned,
    _In_opt_ LPOVERLAPPED Overlapped)
{
    OVERLAPPED Local;
    DWORD Transferred = 0;
    BOOL Result;

    if (Overlapped)
        return DeviceIoControl(File, Code, Input, InputLength, Output, OutputLength, NULL, Overlapped);

    ZeroMemory(&Local, sizeof(Local));
    Local.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (!Local.hEvent)
        return FALSE;
    Result = DeviceIoControl(File, Code, Input, InputLength, Output, OutputLength, NULL, &Local);
    if (Result || GetLastError() == ERROR_IO_PENDING)
        Result = GetOverlappedResult(File, &Local, &Transferred, TRUE);
    CloseHandle(Local.hEvent);
    if (Returned)
        *Returned = Transferred;
    return Result;
}

static
VOID
WinUsbReleaseRoot(
    _In_ PWINUSB_ROOT Root)
{
    if (InterlockedDecrement(&Root->References))
        return;
    DeleteCriticalSection(&Root->Lock);
    HeapFree(GetProcessHeap(), 0, Root->Information);
    HeapFree(GetProcessHeap(), 0, Root);
}

static
PUSB_INTERFACE_DESCRIPTOR
WinUsbFindSetting(
    _In_ PWINUSB_HANDLE Handle,
    _In_ UCHAR SettingIndex)
{
    PLNT_WINUSB_INFORMATION Information = Handle->Root->Information;
    UCHAR Number = Information->InterfaceNumbers[Handle->InterfaceIndex];
    PUCHAR At = Information->Configuration;
    PUCHAR End = At + Information->ConfigurationLength;
    PUSB_INTERFACE_DESCRIPTOR Descriptor;
    UCHAR Found = 0;

    while (At + 2 <= End && At[0] >= 2 && At + At[0] <= End)
    {
        if (At[1] == USB_INTERFACE_DESCRIPTOR_TYPE && At[0] >= sizeof(USB_INTERFACE_DESCRIPTOR))
        {
            Descriptor = (PUSB_INTERFACE_DESCRIPTOR)At;
            if (Descriptor->bInterfaceNumber == Number && Found++ == SettingIndex)
                return Descriptor;
        }
        At += At[0];
    }
    return NULL;
}

static
BOOL
WinUsbFindEndpoint(
    _In_ PWINUSB_HANDLE Handle,
    _In_ UCHAR SettingIndex,
    _In_ UCHAR PipeIndex,
    _Out_ PUSB_ENDPOINT_DESCRIPTOR *Endpoint,
    _Out_ PUCHAR *Companion)
{
    PLNT_WINUSB_INFORMATION Information = Handle->Root->Information;
    PUSB_INTERFACE_DESCRIPTOR Setting = WinUsbFindSetting(Handle, SettingIndex);
    PUCHAR End = Information->Configuration + Information->ConfigurationLength;
    PUCHAR At;
    UCHAR Found = 0;

    *Endpoint = NULL;
    *Companion = NULL;
    if (!Setting)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (PipeIndex >= Setting->bNumEndpoints)
    {
        SetLastError(ERROR_NO_MORE_ITEMS);
        return FALSE;
    }

    for (At = (PUCHAR)Setting + Setting->bLength; At + 2 <= End && At[0] >= 2 && At + At[0] <= End; At += At[0])
    {
        if (At[1] == USB_INTERFACE_DESCRIPTOR_TYPE)
            break;
        if (At[1] == USB_ENDPOINT_DESCRIPTOR_TYPE && At[0] >= sizeof(USB_ENDPOINT_DESCRIPTOR))
        {
            if (*Endpoint)
                break;
            if (Found++ == PipeIndex)
                *Endpoint = (PUSB_ENDPOINT_DESCRIPTOR)At;
        }
        else if (*Endpoint && At[1] == USB_SUPERSPEED_ENDPOINT_COMPANION_DESCRIPTOR_TYPE && At[0] >= 6)
        {
            *Companion = At;
        }
    }
    if (!*Endpoint)
    {
        SetLastError(ERROR_NO_MORE_ITEMS);
        return FALSE;
    }
    return TRUE;
}

BOOL WINAPI WinUsb_Initialize(HANDLE DeviceHandle, PWINUSB_INTERFACE_HANDLE InterfaceHandle)
{
    FILE_MODE_INFORMATION Mode;
    IO_STATUS_BLOCK IoStatus;
    PWINUSB_HANDLE Handle;
    PWINUSB_ROOT Root;
    DWORD Returned;

    if (!InterfaceHandle)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    *InterfaceHandle = NULL;
    if (!DeviceHandle || DeviceHandle == INVALID_HANDLE_VALUE ||
        !NT_SUCCESS(NtQueryInformationFile(DeviceHandle, &IoStatus, &Mode, sizeof(Mode), FileModeInformation)) ||
        (Mode.Mode & (FILE_SYNCHRONOUS_IO_ALERT | FILE_SYNCHRONOUS_IO_NONALERT)))
    {
        SetLastError(ERROR_INVALID_HANDLE);
        return FALSE;
    }

    Root = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*Root));
    Handle = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*Handle));
    if (Root)
        Root->Information = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, WINUSB_INFORMATION_SIZE);
    if (!Root || !Handle || !Root->Information)
    {
        if (Root)
            HeapFree(GetProcessHeap(), 0, Root->Information);
        HeapFree(GetProcessHeap(), 0, Root);
        HeapFree(GetProcessHeap(), 0, Handle);
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }

    if (!WinUsbIoctl(DeviceHandle, IOCTL_LNT_WINUSB_INITIALIZE, NULL, 0, Root->Information, WINUSB_INFORMATION_SIZE,
                     &Returned, NULL))
    {
        DWORD Error = GetLastError();

        HeapFree(GetProcessHeap(), 0, Root->Information);
        HeapFree(GetProcessHeap(), 0, Root);
        HeapFree(GetProcessHeap(), 0, Handle);
        SetLastError(Error);
        return FALSE;
    }
    if (Returned < FIELD_OFFSET(LNT_WINUSB_INFORMATION, Configuration) ||
        Root->Information->Version != LNT_WINUSB_VERSION || !Root->Information->InterfaceCount ||
        Root->Information->InterfaceCount > LNT_WINUSB_MAX_INTERFACES ||
        Returned < FIELD_OFFSET(LNT_WINUSB_INFORMATION, Configuration) + Root->Information->ConfigurationLength)
    {
        HeapFree(GetProcessHeap(), 0, Root->Information);
        HeapFree(GetProcessHeap(), 0, Root);
        HeapFree(GetProcessHeap(), 0, Handle);
        SetLastError(ERROR_BAD_DEVICE);
        return FALSE;
    }

    Root->References = 1;
    Root->File = DeviceHandle;
    InitializeCriticalSection(&Root->Lock);
    Handle->Magic = WINUSB_HANDLE_MAGIC;
    Handle->Root = Root;
    Handle->InterfaceIndex = 0;
    *InterfaceHandle = Handle;
    return TRUE;
}

BOOL WINAPI WinUsb_Free(WINUSB_INTERFACE_HANDLE InterfaceHandle)
{
    PWINUSB_HANDLE Handle = InterfaceHandle;
    PWINUSB_ROOT Root;

    if (!Handle || Handle->Magic != WINUSB_HANDLE_MAGIC)
        return TRUE;
    Root = Handle->Root;
    if (Handle->InterfaceIndex)
    {
        EnterCriticalSection(&Root->Lock);
        Root->Claimed &= ~(1UL << Handle->InterfaceIndex);
        LeaveCriticalSection(&Root->Lock);
    }
    Handle->Magic = 0;
    HeapFree(GetProcessHeap(), 0, Handle);
    WinUsbReleaseRoot(Root);
    return TRUE;
}

BOOL WINAPI WinUsb_GetAssociatedInterface(WINUSB_INTERFACE_HANDLE InterfaceHandle,
                                          UCHAR AssociatedInterfaceIndex,
                                          PWINUSB_INTERFACE_HANDLE AssociatedInterfaceHandle)
{
    PWINUSB_HANDLE Handle = WinUsbHandle(InterfaceHandle), Associated;
    PWINUSB_ROOT Root;
    ULONG Index;

    if (!Handle)
        return FALSE;
    if (Handle->InterfaceIndex)
    {
        SetLastError(ERROR_INVALID_HANDLE);
        return FALSE;
    }
    if (!AssociatedInterfaceHandle || AssociatedInterfaceIndex == 0xFF)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    Root = Handle->Root;
    Index = AssociatedInterfaceIndex + 1;
    if (Index >= Root->Information->InterfaceCount)
    {
        SetLastError(ERROR_NO_MORE_ITEMS);
        return FALSE;
    }

    EnterCriticalSection(&Root->Lock);
    if (Root->Claimed & (1UL << Index))
    {
        LeaveCriticalSection(&Root->Lock);
        SetLastError(ERROR_ALREADY_EXISTS);
        return FALSE;
    }
    Associated = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*Associated));
    if (!Associated)
    {
        LeaveCriticalSection(&Root->Lock);
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }
    Root->Claimed |= 1UL << Index;
    InterlockedIncrement(&Root->References);
    LeaveCriticalSection(&Root->Lock);

    Associated->Magic = WINUSB_HANDLE_MAGIC;
    Associated->Root = Root;
    Associated->InterfaceIndex = (UCHAR)Index;
    *AssociatedInterfaceHandle = Associated;
    return TRUE;
}

BOOL WINAPI WinUsb_GetDescriptor(WINUSB_INTERFACE_HANDLE InterfaceHandle,
                                 UCHAR DescriptorType,
                                 UCHAR Index,
                                 USHORT LanguageID,
                                 PUCHAR Buffer,
                                 ULONG BufferLength,
                                 PULONG LengthTransferred)
{
    PWINUSB_HANDLE Handle = WinUsbHandle(InterfaceHandle);
    LNT_WINUSB_DESCRIPTOR_REQUEST Request;
    PLNT_WINUSB_INFORMATION Information;
    DWORD Returned = 0;
    ULONG Length;
    BOOL Result;

    if (!Handle)
        return FALSE;
    if (LengthTransferred)
        *LengthTransferred = 0;
    if (!LengthTransferred || (!Buffer && BufferLength))
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    Information = Handle->Root->Information;
    if (DescriptorType == USB_DEVICE_DESCRIPTOR_TYPE)
    {
        if (BufferLength < sizeof(Information->DeviceDescriptor))
        {
            SetLastError(ERROR_INSUFFICIENT_BUFFER);
            return FALSE;
        }
        CopyMemory(Buffer, Information->DeviceDescriptor, sizeof(Information->DeviceDescriptor));
        *LengthTransferred = sizeof(Information->DeviceDescriptor);
        return TRUE;
    }
    if (DescriptorType == USB_CONFIGURATION_DESCRIPTOR_TYPE)
    {
        Length = min(BufferLength, Information->ConfigurationLength);
        CopyMemory(Buffer, Information->Configuration, Length);
        *LengthTransferred = Length;
        return TRUE;
    }

    ZeroMemory(&Request, sizeof(Request));
    Request.InterfaceIndex = Handle->InterfaceIndex;
    Request.DescriptorType = DescriptorType;
    Request.Index = Index;
    Request.LanguageId = LanguageID;
    Result = WinUsbIoctl(Handle->Root->File, IOCTL_LNT_WINUSB_GET_DESCRIPTOR, &Request, sizeof(Request), Buffer,
                         BufferLength, &Returned, NULL);
    *LengthTransferred = Result ? Returned : 0;
    return Result;
}

BOOL WINAPI WinUsb_QueryInterfaceSettings(WINUSB_INTERFACE_HANDLE InterfaceHandle,
                                          UCHAR AlternateInterfaceNumber,
                                          PUSB_INTERFACE_DESCRIPTOR UsbAltInterfaceDescriptor)
{
    PWINUSB_HANDLE Handle = WinUsbHandle(InterfaceHandle);
    PUSB_INTERFACE_DESCRIPTOR Setting;

    if (!Handle)
        return FALSE;
    if (!UsbAltInterfaceDescriptor)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    Setting = WinUsbFindSetting(Handle, AlternateInterfaceNumber);
    if (!Setting)
    {
        SetLastError(ERROR_NO_MORE_ITEMS);
        return FALSE;
    }
    CopyMemory(UsbAltInterfaceDescriptor, Setting, sizeof(*UsbAltInterfaceDescriptor));
    return TRUE;
}

BOOL WINAPI WinUsb_QueryDeviceInformation(WINUSB_INTERFACE_HANDLE InterfaceHandle,
                                          ULONG InformationType,
                                          PULONG BufferLength,
                                          PVOID Buffer)
{
    PWINUSB_HANDLE Handle = WinUsbHandle(InterfaceHandle);

    if (!Handle)
        return FALSE;
    if (InformationType != DEVICE_SPEED || !BufferLength)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (*BufferLength < sizeof(UCHAR))
    {
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return FALSE;
    }
    if (!Buffer)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    *(PUCHAR)Buffer = Handle->Root->Information->Speed;
    *BufferLength = sizeof(UCHAR);
    return TRUE;
}

static
BOOL
WinUsbSettingRequest(
    _In_ PWINUSB_HANDLE Handle,
    _In_ DWORD Code,
    _Inout_ PLNT_WINUSB_SETTING_REQUEST Request)
{
    Request->InterfaceIndex = Handle->InterfaceIndex;
    Request->Reserved = 0;
    return WinUsbIoctl(Handle->Root->File, Code, Request, sizeof(*Request), Request, sizeof(*Request), NULL, NULL);
}

BOOL WINAPI WinUsb_SetCurrentAlternateSetting(WINUSB_INTERFACE_HANDLE InterfaceHandle, UCHAR SettingNumber)
{
    PWINUSB_HANDLE Handle = WinUsbHandle(InterfaceHandle);
    LNT_WINUSB_SETTING_REQUEST Request;

    if (!Handle)
        return FALSE;
    Request.AlternateSetting = SettingNumber;
    return WinUsbSettingRequest(Handle, IOCTL_LNT_WINUSB_SET_SETTING, &Request);
}

BOOL WINAPI WinUsb_GetCurrentAlternateSetting(WINUSB_INTERFACE_HANDLE InterfaceHandle, PUCHAR SettingNumber)
{
    PWINUSB_HANDLE Handle = WinUsbHandle(InterfaceHandle);
    LNT_WINUSB_SETTING_REQUEST Request;

    if (!Handle)
        return FALSE;
    if (!SettingNumber)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    Request.AlternateSetting = 0;
    if (!WinUsbSettingRequest(Handle, IOCTL_LNT_WINUSB_GET_SETTING, &Request))
        return FALSE;
    *SettingNumber = Request.AlternateSetting;
    return TRUE;
}

static
VOID
WinUsbFillPipeInformation(
    _In_ PUSB_ENDPOINT_DESCRIPTOR Endpoint,
    _In_opt_ PUCHAR Companion,
    _Out_ PWINUSB_PIPE_INFORMATION_EX Information)
{
    USHORT MaxPacket = Endpoint->wMaxPacketSize & 0x07FF;

    Information->PipeType = (USBD_PIPE_TYPE)(Endpoint->bmAttributes & USB_ENDPOINT_TYPE_MASK);
    Information->PipeId = Endpoint->bEndpointAddress;
    Information->MaximumPacketSize = MaxPacket;
    Information->Interval = Endpoint->bInterval;
    if (Companion && (Information->PipeType == UsbdPipeTypeIsochronous ||
                      Information->PipeType == UsbdPipeTypeInterrupt))
        Information->MaximumBytesPerInterval = Companion[4] | (Companion[5] << 8);
    else
        Information->MaximumBytesPerInterval = MaxPacket * (((Endpoint->wMaxPacketSize >> 11) & 3) + 1);
}

BOOL WINAPI WinUsb_QueryPipe(WINUSB_INTERFACE_HANDLE InterfaceHandle,
                             UCHAR AlternateInterfaceNumber,
                             UCHAR PipeIndex,
                             PWINUSB_PIPE_INFORMATION PipeInformation)
{
    PWINUSB_HANDLE Handle = WinUsbHandle(InterfaceHandle);
    WINUSB_PIPE_INFORMATION_EX Information;
    PUSB_ENDPOINT_DESCRIPTOR Endpoint;
    PUCHAR Companion;

    if (!Handle)
        return FALSE;
    if (!PipeInformation)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (!WinUsbFindEndpoint(Handle, AlternateInterfaceNumber, PipeIndex, &Endpoint, &Companion))
        return FALSE;
    WinUsbFillPipeInformation(Endpoint, Companion, &Information);
    PipeInformation->PipeType = Information.PipeType;
    PipeInformation->PipeId = Information.PipeId;
    PipeInformation->MaximumPacketSize = Information.MaximumPacketSize;
    PipeInformation->Interval = Information.Interval;
    return TRUE;
}

BOOL WINAPI WinUsb_QueryPipeEx(WINUSB_INTERFACE_HANDLE InterfaceHandle,
                               UCHAR AlternateSettingNumber,
                               UCHAR PipeIndex,
                               PWINUSB_PIPE_INFORMATION_EX PipeInformationEx)
{
    PWINUSB_HANDLE Handle = WinUsbHandle(InterfaceHandle);
    PUSB_ENDPOINT_DESCRIPTOR Endpoint;
    PUCHAR Companion;

    if (!Handle)
        return FALSE;
    if (!PipeInformationEx)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (!WinUsbFindEndpoint(Handle, AlternateSettingNumber, PipeIndex, &Endpoint, &Companion))
        return FALSE;
    WinUsbFillPipeInformation(Endpoint, Companion, PipeInformationEx);
    return TRUE;
}

static
BOOL
WinUsbPolicy(
    _In_ PWINUSB_HANDLE Handle,
    _In_ DWORD Code,
    _In_ UCHAR PipeId,
    _In_ ULONG PolicyType,
    _Inout_ PLNT_WINUSB_POLICY_REQUEST Request)
{
    Request->InterfaceIndex = Handle->InterfaceIndex;
    Request->PipeId = PipeId;
    Request->Reserved = 0;
    Request->PolicyType = PolicyType;
    return WinUsbIoctl(Handle->Root->File, Code, Request, sizeof(*Request), Request, sizeof(*Request), NULL, NULL);
}

static
BOOL
WinUsbSetPolicy(
    _In_ WINUSB_INTERFACE_HANDLE InterfaceHandle,
    _In_ DWORD Code,
    _In_ UCHAR PipeId,
    _In_ ULONG PolicyType,
    _In_ ULONG ValueLength,
    _In_reads_bytes_(ValueLength) PVOID Value)
{
    PWINUSB_HANDLE Handle = WinUsbHandle(InterfaceHandle);
    LNT_WINUSB_POLICY_REQUEST Request;

    if (!Handle)
        return FALSE;
    if (ValueLength && !Value)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    ZeroMemory(&Request, sizeof(Request));
    Request.ValueLength = ValueLength;
    CopyMemory(&Request.Value, Value, min(ValueLength, sizeof(Request.Value)));
    return WinUsbPolicy(Handle, Code, PipeId, PolicyType, &Request);
}

static
BOOL
WinUsbGetPolicy(
    _In_ WINUSB_INTERFACE_HANDLE InterfaceHandle,
    _In_ DWORD Code,
    _In_ UCHAR PipeId,
    _In_ ULONG PolicyType,
    _Inout_ PULONG ValueLength,
    _Out_writes_bytes_(*ValueLength) PVOID Value,
    _In_ DWORD ShortError)
{
    PWINUSB_HANDLE Handle = WinUsbHandle(InterfaceHandle);
    LNT_WINUSB_POLICY_REQUEST Request;

    if (!Handle)
        return FALSE;
    if (!ValueLength)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    ZeroMemory(&Request, sizeof(Request));
    Request.ValueLength = sizeof(Request.Value);
    if (!WinUsbPolicy(Handle, Code, PipeId, PolicyType, &Request))
        return FALSE;
    if (*ValueLength < Request.ValueLength || !Value)
    {
        SetLastError(ShortError);
        return FALSE;
    }
    CopyMemory(Value, &Request.Value, Request.ValueLength);
    *ValueLength = Request.ValueLength;
    return TRUE;
}

BOOL WINAPI WinUsb_SetPipePolicy(WINUSB_INTERFACE_HANDLE InterfaceHandle,
                                 UCHAR PipeID,
                                 ULONG PolicyType,
                                 ULONG ValueLength,
                                 PVOID Value)
{
    return WinUsbSetPolicy(InterfaceHandle, IOCTL_LNT_WINUSB_SET_PIPE_POLICY, PipeID, PolicyType, ValueLength, Value);
}

BOOL WINAPI WinUsb_GetPipePolicy(WINUSB_INTERFACE_HANDLE InterfaceHandle,
                                 UCHAR PipeID,
                                 ULONG PolicyType,
                                 PULONG ValueLength,
                                 PVOID Value)
{
    return WinUsbGetPolicy(InterfaceHandle, IOCTL_LNT_WINUSB_GET_PIPE_POLICY, PipeID, PolicyType, ValueLength, Value,
                           ERROR_INSUFFICIENT_BUFFER);
}

BOOL WINAPI WinUsb_SetPowerPolicy(WINUSB_INTERFACE_HANDLE InterfaceHandle,
                                  ULONG PolicyType,
                                  ULONG ValueLength,
                                  PVOID Value)
{
    return WinUsbSetPolicy(InterfaceHandle, IOCTL_LNT_WINUSB_SET_POWER_POLICY, 0, PolicyType, ValueLength, Value);
}

BOOL WINAPI WinUsb_GetPowerPolicy(WINUSB_INTERFACE_HANDLE InterfaceHandle,
                                  ULONG PolicyType,
                                  PULONG ValueLength,
                                  PVOID Value)
{
    return WinUsbGetPolicy(InterfaceHandle, IOCTL_LNT_WINUSB_GET_POWER_POLICY, 0, PolicyType, ValueLength, Value,
                           ERROR_INVALID_PARAMETER);
}

static
BOOL
WinUsbTransfer(
    _In_ WINUSB_INTERFACE_HANDLE InterfaceHandle,
    _In_ DWORD Code,
    _In_ UCHAR PipeID,
    _In_reads_bytes_(BufferLength) PVOID Buffer,
    _In_ ULONG BufferLength,
    _Out_opt_ PULONG LengthTransferred,
    _In_opt_ LPOVERLAPPED Overlapped)
{
    PWINUSB_HANDLE Handle = WinUsbHandle(InterfaceHandle);
    LNT_WINUSB_PIPE_REQUEST Request;
    DWORD Returned = 0;
    BOOL Result;

    if (!Handle)
        return FALSE;
    if (!Buffer && BufferLength)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    Request.InterfaceIndex = Handle->InterfaceIndex;
    Request.PipeId = PipeID;
    Request.Reserved = 0;
    Result = WinUsbIoctl(Handle->Root->File, Code, &Request, sizeof(Request), Buffer, BufferLength, &Returned,
                         Overlapped);
    if (!Overlapped && LengthTransferred)
        *LengthTransferred = Returned;
    return Result;
}

BOOL WINAPI WinUsb_ReadPipe(WINUSB_INTERFACE_HANDLE InterfaceHandle,
                            UCHAR PipeID,
                            PUCHAR Buffer,
                            ULONG BufferLength,
                            PULONG LengthTransferred,
                            LPOVERLAPPED Overlapped)
{
    return WinUsbTransfer(InterfaceHandle, IOCTL_LNT_WINUSB_READ_PIPE, PipeID, Buffer, BufferLength,
                          LengthTransferred, Overlapped);
}

BOOL WINAPI WinUsb_WritePipe(WINUSB_INTERFACE_HANDLE InterfaceHandle,
                             UCHAR PipeID,
                             PUCHAR Buffer,
                             ULONG BufferLength,
                             PULONG LengthTransferred,
                             LPOVERLAPPED Overlapped)
{
    return WinUsbTransfer(InterfaceHandle, IOCTL_LNT_WINUSB_WRITE_PIPE, PipeID, Buffer, BufferLength,
                          LengthTransferred, Overlapped);
}

BOOL WINAPI WinUsb_ControlTransfer(WINUSB_INTERFACE_HANDLE InterfaceHandle,
                                   WINUSB_SETUP_PACKET SetupPacket,
                                   PUCHAR Buffer,
                                   ULONG BufferLength,
                                   PULONG LengthTransferred,
                                   LPOVERLAPPED Overlapped)
{
    PWINUSB_HANDLE Handle = WinUsbHandle(InterfaceHandle);
    LNT_WINUSB_CONTROL_REQUEST Request;
    DWORD Returned = 0;
    BOOL Result;

    if (!Handle)
        return FALSE;
    if (!Buffer && BufferLength)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    ZeroMemory(&Request, sizeof(Request));
    Request.InterfaceIndex = Handle->InterfaceIndex;
    CopyMemory(Request.SetupPacket, &SetupPacket, sizeof(Request.SetupPacket));
    Result = WinUsbIoctl(Handle->Root->File,
                         (SetupPacket.RequestType & 0x80) ? IOCTL_LNT_WINUSB_CONTROL_IN : IOCTL_LNT_WINUSB_CONTROL_OUT,
                         &Request, sizeof(Request), Buffer, BufferLength, &Returned, Overlapped);
    if (!Overlapped && LengthTransferred)
        *LengthTransferred = Returned;
    return Result;
}

static
BOOL
WinUsbPipeRequest(
    _In_ WINUSB_INTERFACE_HANDLE InterfaceHandle,
    _In_ DWORD Code,
    _In_ UCHAR PipeID)
{
    PWINUSB_HANDLE Handle = WinUsbHandle(InterfaceHandle);
    LNT_WINUSB_PIPE_REQUEST Request;

    if (!Handle)
        return FALSE;
    Request.InterfaceIndex = Handle->InterfaceIndex;
    Request.PipeId = PipeID;
    Request.Reserved = 0;
    return WinUsbIoctl(Handle->Root->File, Code, &Request, sizeof(Request), NULL, 0, NULL, NULL);
}

BOOL WINAPI WinUsb_ResetPipe(WINUSB_INTERFACE_HANDLE InterfaceHandle, UCHAR PipeID)
{
    return WinUsbPipeRequest(InterfaceHandle, IOCTL_LNT_WINUSB_RESET_PIPE, PipeID);
}

BOOL WINAPI WinUsb_AbortPipe(WINUSB_INTERFACE_HANDLE InterfaceHandle, UCHAR PipeID)
{
    return WinUsbPipeRequest(InterfaceHandle, IOCTL_LNT_WINUSB_ABORT_PIPE, PipeID);
}

BOOL WINAPI WinUsb_FlushPipe(WINUSB_INTERFACE_HANDLE InterfaceHandle, UCHAR PipeID)
{
    return WinUsbPipeRequest(InterfaceHandle, IOCTL_LNT_WINUSB_FLUSH_PIPE, PipeID);
}

BOOL WINAPI WinUsb_GetOverlappedResult(WINUSB_INTERFACE_HANDLE InterfaceHandle,
                                       LPOVERLAPPED lpOverlapped,
                                       LPDWORD lpNumberOfBytesTransferred,
                                       BOOL bWait)
{
    PWINUSB_HANDLE Handle = WinUsbHandle(InterfaceHandle);

    if (!Handle)
        return FALSE;
    return GetOverlappedResult(Handle->Root->File, lpOverlapped, lpNumberOfBytesTransferred, bWait);
}

PUSB_INTERFACE_DESCRIPTOR WINAPI WinUsb_ParseConfigurationDescriptor(
    PUSB_CONFIGURATION_DESCRIPTOR ConfigurationDescriptor, PVOID StartPosition, LONG InterfaceNumber,
    LONG AlternateSetting, LONG InterfaceClass, LONG InterfaceSubClass, LONG InterfaceProtocol)
{
    PUSB_INTERFACE_DESCRIPTOR Descriptor;
    PUCHAR At = StartPosition, End;

    if (!ConfigurationDescriptor || !At)
        return NULL;
    End = (PUCHAR)ConfigurationDescriptor + ConfigurationDescriptor->wTotalLength;
    while (At + 2 <= End && At[0] >= 2 && At + At[0] <= End)
    {
        Descriptor = (PUSB_INTERFACE_DESCRIPTOR)At;
        if (At[1] == USB_INTERFACE_DESCRIPTOR_TYPE && At[0] >= sizeof(USB_INTERFACE_DESCRIPTOR) &&
            (InterfaceNumber == -1 || Descriptor->bInterfaceNumber == InterfaceNumber) &&
            (AlternateSetting == -1 || Descriptor->bAlternateSetting == AlternateSetting) &&
            (InterfaceClass == -1 || Descriptor->bInterfaceClass == InterfaceClass) &&
            (InterfaceSubClass == -1 || Descriptor->bInterfaceSubClass == InterfaceSubClass) &&
            (InterfaceProtocol == -1 || Descriptor->bInterfaceProtocol == InterfaceProtocol))
        {
            return Descriptor;
        }
        At += At[0];
    }
    return NULL;
}

PUSB_COMMON_DESCRIPTOR WINAPI WinUsb_ParseDescriptors(PVOID DescriptorBuffer, ULONG TotalLength, PVOID StartPosition,
                                                      LONG DescriptorType)
{
    PUCHAR At = StartPosition, End;

    if (!DescriptorBuffer || !At || At < (PUCHAR)DescriptorBuffer)
        return NULL;
    End = (PUCHAR)DescriptorBuffer + TotalLength;
    while (At + 2 <= End && At[0] >= 2 && At + At[0] <= End)
    {
        if (DescriptorType == -1 || At[1] == DescriptorType)
            return (PUSB_COMMON_DESCRIPTOR)At;
        At += At[0];
    }
    return NULL;
}

BOOL WINAPI WinUsb_GetCurrentFrameNumber(WINUSB_INTERFACE_HANDLE InterfaceHandle, PULONG CurrentFrameNumber,
                                         LARGE_INTEGER *TimeStamp)
{
    PWINUSB_HANDLE Handle = WinUsbHandle(InterfaceHandle);
    LNT_WINUSB_FRAME_NUMBER Frame;

    if (!Handle)
        return FALSE;
    if (!CurrentFrameNumber || !TimeStamp)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (!WinUsbIoctl(Handle->Root->File, IOCTL_LNT_WINUSB_GET_FRAME_NUMBER, NULL, 0, &Frame, sizeof(Frame), NULL,
                     NULL))
    {
        return FALSE;
    }
    *CurrentFrameNumber = Frame.FrameNumber;
    *TimeStamp = Frame.TimeStamp;
    return TRUE;
}

BOOL WINAPI WinUsb_GetAdjustedFrameNumber(PULONG CurrentFrameNumber, LARGE_INTEGER TimeStamp)
{
    LARGE_INTEGER Now, Frequency;

    if (!CurrentFrameNumber || !QueryPerformanceCounter(&Now) || !QueryPerformanceFrequency(&Frequency) ||
        !Frequency.QuadPart)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (Now.QuadPart > TimeStamp.QuadPart)
        *CurrentFrameNumber += (ULONG)((Now.QuadPart - TimeStamp.QuadPart) * 1000 / Frequency.QuadPart);
    return TRUE;
}

static
BOOL
WinUsbIsochUnsupported(VOID)
{
    /* TODO: isochronous WinUSB transfers and USB time synchronization need isochronous pipes in winusb.sys */
    SetLastError(ERROR_NOT_SUPPORTED);
    return FALSE;
}

BOOL WINAPI WinUsb_RegisterIsochBuffer(WINUSB_INTERFACE_HANDLE InterfaceHandle, UCHAR PipeID, PUCHAR Buffer,
                                       ULONG BufferLength, PWINUSB_ISOCH_BUFFER_HANDLE IsochBufferHandle)
{
    if (IsochBufferHandle)
        *IsochBufferHandle = NULL;
    if (!WinUsbHandle(InterfaceHandle))
        return FALSE;
    return WinUsbIsochUnsupported();
}

BOOL WINAPI WinUsb_UnregisterIsochBuffer(WINUSB_ISOCH_BUFFER_HANDLE IsochBufferHandle)
{
    return WinUsbIsochUnsupported();
}

BOOL WINAPI WinUsb_WriteIsochPipe(WINUSB_ISOCH_BUFFER_HANDLE BufferHandle, ULONG Offset, ULONG Length,
                                  PULONG FrameNumber, LPOVERLAPPED Overlapped)
{
    return WinUsbIsochUnsupported();
}

BOOL WINAPI WinUsb_ReadIsochPipe(WINUSB_ISOCH_BUFFER_HANDLE BufferHandle, ULONG Offset, ULONG Length,
                                 PULONG FrameNumber, ULONG NumberOfPackets,
                                 PUSBD_ISO_PACKET_DESCRIPTOR IsoPacketDescriptors, LPOVERLAPPED Overlapped)
{
    return WinUsbIsochUnsupported();
}

BOOL WINAPI WinUsb_WriteIsochPipeAsap(WINUSB_ISOCH_BUFFER_HANDLE BufferHandle, ULONG Offset, ULONG Length,
                                      BOOL ContinueStream, LPOVERLAPPED Overlapped)
{
    return WinUsbIsochUnsupported();
}

BOOL WINAPI WinUsb_ReadIsochPipeAsap(WINUSB_ISOCH_BUFFER_HANDLE BufferHandle, ULONG Offset, ULONG Length,
                                     BOOL ContinueStream, ULONG NumberOfPackets,
                                     PUSBD_ISO_PACKET_DESCRIPTOR IsoPacketDescriptors, LPOVERLAPPED Overlapped)
{
    return WinUsbIsochUnsupported();
}

BOOL WINAPI WinUsb_StartTrackingForTimeSync(WINUSB_INTERFACE_HANDLE InterfaceHandle,
                                            PUSB_START_TRACKING_FOR_TIME_SYNC_INFORMATION StartTrackingInfo)
{
    if (!WinUsbHandle(InterfaceHandle))
        return FALSE;
    if (!StartTrackingInfo)
    {
        SetLastError(ERROR_INVALID_HANDLE);
        return FALSE;
    }
    return WinUsbIsochUnsupported();
}

BOOL WINAPI WinUsb_GetCurrentFrameNumberAndQpc(WINUSB_INTERFACE_HANDLE InterfaceHandle,
                                               PUSB_FRAME_NUMBER_AND_QPC_FOR_TIME_SYNC_INFORMATION FrameQpcInfo)
{
    if (!WinUsbHandle(InterfaceHandle))
        return FALSE;
    if (!FrameQpcInfo)
    {
        SetLastError(ERROR_INVALID_HANDLE);
        return FALSE;
    }
    return WinUsbIsochUnsupported();
}

BOOL WINAPI WinUsb_StopTrackingForTimeSync(WINUSB_INTERFACE_HANDLE InterfaceHandle,
                                           PUSB_STOP_TRACKING_FOR_TIME_SYNC_INFORMATION StopTrackingInfo)
{
    if (!WinUsbHandle(InterfaceHandle))
        return FALSE;
    if (!StopTrackingInfo)
    {
        SetLastError(ERROR_INVALID_HANDLE);
        return FALSE;
    }
    return WinUsbIsochUnsupported();
}
