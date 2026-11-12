/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Device access helpers for the WinUSB tests
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "winusb_test.h"

BOOL FindInterfacePath(const GUID *Guid, ULONG Index, PWSTR Path, ULONG PathChars)
{
    SP_DEVICE_INTERFACE_DATA Interface;
    PSP_DEVICE_INTERFACE_DETAIL_DATA_W Detail;
    BYTE Buffer[sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W) + 1024];
    HDEVINFO Set;
    BOOL Found = FALSE;

    Set = SetupDiGetClassDevsW(Guid, NULL, NULL, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (Set == INVALID_HANDLE_VALUE)
        return FALSE;
    Interface.cbSize = sizeof(Interface);
    if (SetupDiEnumDeviceInterfaces(Set, NULL, Guid, Index, &Interface))
    {
        Detail = (PSP_DEVICE_INTERFACE_DETAIL_DATA_W)Buffer;
        Detail->cbSize = sizeof(*Detail);
        if (SetupDiGetDeviceInterfaceDetailW(Set, &Interface, Detail, sizeof(Buffer), NULL, NULL))
        {
            lstrcpynW(Path, Detail->DevicePath, PathChars);
            Found = TRUE;
        }
    }
    SetupDiDestroyDeviceInfoList(Set);
    return Found;
}

BOOL OpenTestDevice(const GUID *Guid, PTEST_DEVICE Device)
{
    ZeroMemory(Device, sizeof(*Device));
    Device->File = INVALID_HANDLE_VALUE;
    if (!FindInterfacePath(Guid, 0, Device->Path, ARRAYSIZE(Device->Path)))
        return FALSE;
    Device->File = CreateFileW(Device->Path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                               OPEN_EXISTING, FILE_FLAG_OVERLAPPED, NULL);
    if (Device->File == INVALID_HANDLE_VALUE)
        return FALSE;
    if (!WinUsb_Initialize(Device->File, &Device->Interface))
    {
        CloseHandle(Device->File);
        Device->File = INVALID_HANDLE_VALUE;
        return FALSE;
    }
    return TRUE;
}

VOID CloseTestDevice(PTEST_DEVICE Device)
{
    if (Device->Interface)
        WinUsb_Free(Device->Interface);
    if (Device->File != INVALID_HANDLE_VALUE)
        CloseHandle(Device->File);
    Device->Interface = NULL;
    Device->File = INVALID_HANDLE_VALUE;
}

BOOL VendorOut(WINUSB_INTERFACE_HANDLE Interface, UCHAR Request, USHORT Value, USHORT Index, PVOID Data,
               USHORT Length)
{
    WINUSB_SETUP_PACKET Setup;
    ULONG Transferred = 0;

    Setup.RequestType = 0x40;
    Setup.Request = Request;
    Setup.Value = Value;
    Setup.Index = Index;
    Setup.Length = Length;
    return WinUsb_ControlTransfer(Interface, Setup, Data, Length, &Transferred, NULL);
}

BOOL VendorIn(WINUSB_INTERFACE_HANDLE Interface, UCHAR Request, USHORT Value, USHORT Index, PVOID Data,
              USHORT Length, PULONG Transferred)
{
    WINUSB_SETUP_PACKET Setup;

    Setup.RequestType = 0xC0;
    Setup.Request = Request;
    Setup.Value = Value;
    Setup.Index = Index;
    Setup.Length = Length;
    *Transferred = 0;
    return WinUsb_ControlTransfer(Interface, Setup, Data, Length, Transferred, NULL);
}

BOOL GetDeviceStatus(WINUSB_INTERFACE_HANDLE Interface, PDEVICE_STATUS Status)
{
    ULONG Transferred;

    ZeroMemory(Status, sizeof(*Status));
    if (!VendorIn(Interface, REQ_STATUS, 0, 0, Status, sizeof(*Status), &Transferred))
        return FALSE;
    return Transferred == sizeof(*Status) && Status->Magic == 0x42535557;
}

BOOL ResetDevice(WINUSB_INTERFACE_HANDLE Interface)
{
    return VendorOut(Interface, REQ_RESET, 0, 0, NULL, 0);
}

static DWORD TransferWait(PTEST_DEVICE Device, WINUSB_INTERFACE_HANDLE Interface, UCHAR Pipe, PVOID Buffer,
                          ULONG Length, PULONG Transferred, DWORD TimeoutMs, BOOL Read)
{
    OVERLAPPED Overlapped;
    DWORD Error = ERROR_SUCCESS;
    BOOL Result;

    *Transferred = 0;
    ZeroMemory(&Overlapped, sizeof(Overlapped));
    Overlapped.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (Read)
        Result = WinUsb_ReadPipe(Interface, Pipe, Buffer, Length, NULL, &Overlapped);
    else
        Result = WinUsb_WritePipe(Interface, Pipe, Buffer, Length, NULL, &Overlapped);
    if (!Result)
    {
        Error = GetLastError();
        if (Error != ERROR_IO_PENDING)
        {
            CloseHandle(Overlapped.hEvent);
            return Error;
        }
        if (WaitForSingleObject(Overlapped.hEvent, TimeoutMs) == WAIT_TIMEOUT)
        {
            CancelIoEx(Device->File, &Overlapped);
            WaitForSingleObject(Overlapped.hEvent, 5000);
            WinUsb_GetOverlappedResult(Interface, &Overlapped, Transferred, FALSE);
            CloseHandle(Overlapped.hEvent);
            return TEST_TIMEOUT_ERROR;
        }
    }
    Error = WinUsb_GetOverlappedResult(Interface, &Overlapped, Transferred, FALSE) ? ERROR_SUCCESS : GetLastError();
    CloseHandle(Overlapped.hEvent);
    return Error;
}

DWORD ReadWait(PTEST_DEVICE Device, WINUSB_INTERFACE_HANDLE Interface, UCHAR Pipe, PVOID Buffer, ULONG Length,
               PULONG Transferred, DWORD TimeoutMs)
{
    return TransferWait(Device, Interface, Pipe, Buffer, Length, Transferred, TimeoutMs, TRUE);
}

DWORD WriteWait(PTEST_DEVICE Device, WINUSB_INTERFACE_HANDLE Interface, UCHAR Pipe, PVOID Buffer, ULONG Length,
                PULONG Transferred, DWORD TimeoutMs)
{
    return TransferWait(Device, Interface, Pipe, Buffer, Length, Transferred, TimeoutMs, FALSE);
}

BOOL SetPolicyBool(WINUSB_INTERFACE_HANDLE Interface, UCHAR Pipe, ULONG Policy, UCHAR Value)
{
    return WinUsb_SetPipePolicy(Interface, Pipe, Policy, sizeof(Value), &Value);
}

BOOL SetPolicyUlong(WINUSB_INTERFACE_HANDLE Interface, UCHAR Pipe, ULONG Policy, ULONG Value)
{
    return WinUsb_SetPipePolicy(Interface, Pipe, Policy, sizeof(Value), &Value);
}

VOID FillPattern(PUCHAR Buffer, ULONG Length, ULONG Seed)
{
    ULONG Index;

    for (Index = 0; Index < Length; Index++)
        Buffer[Index] = (UCHAR)(Seed * 31 + Index * 7 + (Index >> 8));
}

BOOL CheckPattern(const UCHAR *Buffer, ULONG Length, ULONG Seed)
{
    ULONG Index;

    for (Index = 0; Index < Length; Index++)
    {
        if (Buffer[Index] != (UCHAR)(Seed * 31 + Index * 7 + (Index >> 8)))
            return FALSE;
    }
    return TRUE;
}

ULONGLONG NowMs(VOID)
{
    LARGE_INTEGER Counter, Frequency;

    QueryPerformanceCounter(&Counter);
    QueryPerformanceFrequency(&Frequency);
    return (ULONGLONG)(Counter.QuadPart * 1000 / Frequency.QuadPart);
}
