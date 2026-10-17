/*
 * PROJECT:     LiberNT system libraries
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     User-mode access to WMI data blocks
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <advapi32.h>

#define _WMI_SOURCE_
#include <wmium.h>
#include <wmiioctl.h>

typedef struct _WMIP_OPEN_GUID_BLOCK
{
    POBJECT_ATTRIBUTES ObjectAttributes;
    ACCESS_MASK DesiredAccess;
    ULONG Reserved1;
    ULONG Reserved2;
} WMIP_OPEN_GUID_BLOCK;

typedef struct _WMIP_OPEN_GUID_RESULT
{
    ULONG Reserved1;
    ULONG Reserved2;
    HANDLE Handle;
    ULONG Reserved3;
} WMIP_OPEN_GUID_RESULT;

static HANDLE WmipDevice = INVALID_HANDLE_VALUE;

static HANDLE
WmipGetDevice(VOID)
{
    HANDLE Device = WmipDevice;
    HANDLE Existing;

    if (Device != INVALID_HANDLE_VALUE)
        return Device;
    Device = CreateFileW(L"\\\\.\\WMIDataDevice", GENERIC_READ | GENERIC_WRITE,
                         FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (Device == INVALID_HANDLE_VALUE)
        Device = CreateFileW(L"\\\\.\\WMIDataDevice", GENERIC_READ,
                             FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (Device == INVALID_HANDLE_VALUE)
        return Device;
    Existing = InterlockedCompareExchangePointer(&WmipDevice, Device, INVALID_HANDLE_VALUE);
    if (Existing != INVALID_HANDLE_VALUE)
    {
        CloseHandle(Device);
        return Existing;
    }
    return Device;
}

static ULONG
WmipTransfer(
    _In_ DWORD IoControlCode,
    _In_reads_bytes_(InputSize) PVOID Input,
    _In_ ULONG InputSize,
    _Inout_opt_ PULONG BufferSize,
    _Out_writes_bytes_opt_(*BufferSize) PVOID Buffer)
{
    ULONG Capacity = (BufferSize != NULL && Buffer != NULL) ? *BufferSize : 0;
    ULONG Scratch = 0;
    PVOID Output = Buffer;
    ULONG OutputSize = Capacity;
    DWORD Returned = 0;
    DWORD Error;
    HANDLE Device;

    Device = WmipGetDevice();
    if (Device == INVALID_HANDLE_VALUE)
        return GetLastError();
    if (OutputSize < sizeof(ULONG))
    {
        Output = &Scratch;
        OutputSize = sizeof(Scratch);
    }
    if (DeviceIoControl(Device, IoControlCode, Input, InputSize, Output, OutputSize, &Returned, NULL))
    {
        if (Returned > Capacity)
        {
            if (BufferSize != NULL)
                *BufferSize = Returned;
            return ERROR_INSUFFICIENT_BUFFER;
        }
        if (Output == &Scratch && Returned != 0)
            RtlCopyMemory(Buffer, &Scratch, Returned);
        if (BufferSize != NULL)
            *BufferSize = Returned;
        return ERROR_SUCCESS;
    }
    Error = GetLastError();
    if (Error == ERROR_MORE_DATA && Returned >= sizeof(ULONG))
    {
        if (BufferSize != NULL)
            *BufferSize = *(PULONG)Output;
        return ERROR_INSUFFICIENT_BUFFER;
    }
    return Error;
}

static PWNODE_HEADER
WmipBuildNode(
    _In_ WMIHANDLE Handle,
    _In_ ULONG HeaderSize,
    _In_ ULONG Flags,
    _In_ LPCWSTR InstanceName,
    _In_ ULONG DataSize,
    _In_reads_bytes_opt_(DataSize) const VOID *Data,
    _Out_ PULONG DataOffset,
    _Out_ PULONG Error)
{
    SIZE_T NameBytes = wcslen(InstanceName) * sizeof(WCHAR);
    SIZE_T Offset;
    SIZE_T Total;
    PWNODE_HEADER Header;

    *DataOffset = 0;
    if (NameBytes > MAXUSHORT)
    {
        *Error = ERROR_INVALID_PARAMETER;
        return NULL;
    }
    Offset = (HeaderSize + sizeof(USHORT) + NameBytes + 7) & ~(SIZE_T)7;
    Total = Offset + DataSize;
    if (Total > MAXULONG || Total < Offset)
    {
        *Error = ERROR_INVALID_PARAMETER;
        return NULL;
    }
    Header = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, Total);
    if (Header == NULL)
    {
        *Error = ERROR_NOT_ENOUGH_MEMORY;
        return NULL;
    }
    Header->BufferSize = (ULONG)Total;
    Header->Version = 1;
    Header->KernelHandle = Handle;
    Header->Flags = Flags;
    *(USHORT UNALIGNED *)((PUCHAR)Header + HeaderSize) = (USHORT)NameBytes;
    RtlCopyMemory((PUCHAR)Header + HeaderSize + sizeof(USHORT), InstanceName, NameBytes);
    if (DataSize != 0)
        RtlCopyMemory((PUCHAR)Header + Offset, Data, DataSize);
    *DataOffset = (ULONG)Offset;
    *Error = ERROR_SUCCESS;
    return Header;
}

static LPWSTR
WmipAnsiToWide(
    _In_ LPCSTR String)
{
    int Count;
    LPWSTR Wide;

    Count = MultiByteToWideChar(CP_ACP, 0, String, -1, NULL, 0);
    if (Count == 0)
        return NULL;
    Wide = HeapAlloc(GetProcessHeap(), 0, Count * sizeof(WCHAR));
    if (Wide != NULL)
        MultiByteToWideChar(CP_ACP, 0, String, -1, Wide, Count);
    return Wide;
}

ULONG
WMIAPI
WmiOpenBlock(
    _In_ GUID *Guid,
    _In_ ULONG DesiredAccess,
    _Out_ WMIHANDLE *DataBlockHandle)
{
    WCHAR Name[48];
    UNICODE_STRING ObjectName;
    OBJECT_ATTRIBUTES Attributes;
    WMIP_OPEN_GUID_BLOCK Open;
    WMIP_OPEN_GUID_RESULT Result;
    DWORD Returned;
    HANDLE Device;

    if (Guid == NULL || DataBlockHandle == NULL)
        return ERROR_INVALID_PARAMETER;
    *DataBlockHandle = NULL;
    Device = WmipGetDevice();
    if (Device == INVALID_HANDLE_VALUE)
        return GetLastError();
    _snwprintf(Name, ARRAYSIZE(Name), L"\\WmiGuid\\%08lX-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X",
             Guid->Data1, Guid->Data2, Guid->Data3, Guid->Data4[0], Guid->Data4[1], Guid->Data4[2],
             Guid->Data4[3], Guid->Data4[4], Guid->Data4[5], Guid->Data4[6], Guid->Data4[7]);
    RtlInitUnicodeString(&ObjectName, Name);
    InitializeObjectAttributes(&Attributes, &ObjectName, 0, NULL, NULL);
    RtlZeroMemory(&Open, sizeof(Open));
    RtlZeroMemory(&Result, sizeof(Result));
    Open.ObjectAttributes = &Attributes;
    Open.DesiredAccess = DesiredAccess;
    if (!DeviceIoControl(Device, IOCTL_WMI_OPEN_BLOCK, &Open, sizeof(Open),
                         &Result, sizeof(Result), &Returned, NULL))
    {
        return GetLastError();
    }
    *DataBlockHandle = Result.Handle;
    return ERROR_SUCCESS;
}

ULONG
WMIAPI
WmiCloseBlock(
    _In_ WMIHANDLE DataBlockHandle)
{
    return CloseHandle(DataBlockHandle) ? ERROR_SUCCESS : GetLastError();
}

ULONG
WMIAPI
WmiQueryAllDataW(
    _In_ WMIHANDLE DataBlockHandle,
    _Inout_ ULONG *BufferSize,
    _Out_writes_bytes_opt_(*BufferSize) LPVOID Buffer)
{
    WNODE_HEADER Header;

    if (BufferSize == NULL)
        return ERROR_INVALID_PARAMETER;
    RtlZeroMemory(&Header, sizeof(Header));
    Header.BufferSize = sizeof(Header);
    Header.Version = 1;
    Header.KernelHandle = DataBlockHandle;
    Header.Flags = WNODE_FLAG_ALL_DATA;
    return WmipTransfer(IOCTL_WMI_QUERY_ALL_DATA, &Header, sizeof(Header), BufferSize, Buffer);
}

ULONG
WMIAPI
WmiQuerySingleInstanceW(
    _In_ WMIHANDLE DataBlockHandle,
    _In_ LPCWSTR InstanceName,
    _Inout_ PULONG BufferSize,
    _Out_writes_bytes_to_opt_(*BufferSize, *BufferSize) LPVOID Buffer)
{
    PWNODE_SINGLE_INSTANCE Node;
    ULONG DataOffset, Error;

    if (InstanceName == NULL || BufferSize == NULL)
        return ERROR_INVALID_PARAMETER;
    Node = (PWNODE_SINGLE_INSTANCE)WmipBuildNode(DataBlockHandle, sizeof(*Node), WNODE_FLAG_SINGLE_INSTANCE,
                                                 InstanceName, 0, NULL, &DataOffset, &Error);
    if (Node == NULL)
        return Error;
    Node->OffsetInstanceName = sizeof(*Node);
    Node->DataBlockOffset = DataOffset;
    Error = WmipTransfer(IOCTL_WMI_SINGLE_INSTANCE, Node, Node->WnodeHeader.BufferSize, BufferSize, Buffer);
    HeapFree(GetProcessHeap(), 0, Node);
    return Error;
}

ULONG
WMIAPI
WmiSetSingleInstanceW(
    _In_ WMIHANDLE DataBlockHandle,
    _In_ LPCWSTR InstanceName,
    _In_ ULONG Reserved,
    _In_ ULONG ValueBufferSize,
    _In_reads_bytes_(ValueBufferSize) PVOID ValueBuffer)
{
    PWNODE_SINGLE_INSTANCE Node;
    ULONG DataOffset, Error;

    UNREFERENCED_PARAMETER(Reserved);
    if (InstanceName == NULL || (ValueBufferSize != 0 && ValueBuffer == NULL))
        return ERROR_INVALID_PARAMETER;
    Node = (PWNODE_SINGLE_INSTANCE)WmipBuildNode(DataBlockHandle, sizeof(*Node), WNODE_FLAG_SINGLE_INSTANCE,
                                                 InstanceName, ValueBufferSize, ValueBuffer, &DataOffset, &Error);
    if (Node == NULL)
        return Error;
    Node->OffsetInstanceName = sizeof(*Node);
    Node->DataBlockOffset = DataOffset;
    Node->SizeDataBlock = ValueBufferSize;
    Error = WmipTransfer(IOCTL_WMI_SET_SINGLE_INSTANCE, Node, Node->WnodeHeader.BufferSize, NULL, NULL);
    HeapFree(GetProcessHeap(), 0, Node);
    return Error;
}

ULONG
WMIAPI
WmiSetSingleItemW(
    _In_ WMIHANDLE DataBlockHandle,
    _In_ LPCWSTR InstanceName,
    _In_ ULONG DataItemId,
    _In_ ULONG Reserved,
    _In_ ULONG ValueBufferSize,
    _In_reads_bytes_(ValueBufferSize) PVOID ValueBuffer)
{
    PWNODE_SINGLE_ITEM Node;
    ULONG DataOffset, Error;

    UNREFERENCED_PARAMETER(Reserved);
    if (InstanceName == NULL || (ValueBufferSize != 0 && ValueBuffer == NULL))
        return ERROR_INVALID_PARAMETER;
    Node = (PWNODE_SINGLE_ITEM)WmipBuildNode(DataBlockHandle, sizeof(*Node), WNODE_FLAG_SINGLE_ITEM,
                                             InstanceName, ValueBufferSize, ValueBuffer, &DataOffset, &Error);
    if (Node == NULL)
        return Error;
    Node->OffsetInstanceName = sizeof(*Node);
    Node->ItemId = DataItemId;
    Node->DataBlockOffset = DataOffset;
    Node->SizeDataItem = ValueBufferSize;
    Error = WmipTransfer(IOCTL_WMI_SET_SINGLE_ITEM, Node, Node->WnodeHeader.BufferSize, NULL, NULL);
    HeapFree(GetProcessHeap(), 0, Node);
    return Error;
}

ULONG
WMIAPI
WmiExecuteMethodW(
    _In_ WMIHANDLE MethodDataBlockHandle,
    _In_ LPCWSTR MethodInstanceName,
    _In_ ULONG MethodId,
    _In_ ULONG InputValueBufferSize,
    _In_reads_bytes_opt_(InputValueBufferSize) PVOID InputValueBuffer,
    _Inout_opt_ ULONG *OutputBufferSize,
    _Out_writes_bytes_opt_(*OutputBufferSize) PVOID OutputBuffer)
{
    PWNODE_METHOD_ITEM Node;
    ULONG DataOffset, Error;

    if (MethodInstanceName == NULL || (InputValueBufferSize != 0 && InputValueBuffer == NULL))
        return ERROR_INVALID_PARAMETER;
    Node = (PWNODE_METHOD_ITEM)WmipBuildNode(MethodDataBlockHandle, sizeof(*Node), WNODE_FLAG_METHOD_ITEM,
                                             MethodInstanceName, InputValueBufferSize, InputValueBuffer,
                                             &DataOffset, &Error);
    if (Node == NULL)
        return Error;
    Node->OffsetInstanceName = sizeof(*Node);
    Node->MethodId = MethodId;
    Node->DataBlockOffset = DataOffset;
    Node->SizeDataBlock = InputValueBufferSize;
    Error = WmipTransfer(IOCTL_WMI_EXECUTE_METHOD, Node, Node->WnodeHeader.BufferSize, OutputBufferSize, OutputBuffer);
    HeapFree(GetProcessHeap(), 0, Node);
    return Error;
}

ULONG
WMIAPI
WmiSetSingleInstanceA(
    _In_ WMIHANDLE DataBlockHandle,
    _In_ LPCSTR InstanceName,
    _In_ ULONG Reserved,
    _In_ ULONG ValueBufferSize,
    _In_reads_bytes_(ValueBufferSize) PVOID ValueBuffer)
{
    LPWSTR Name;
    ULONG Error;

    if (InstanceName == NULL)
        return ERROR_INVALID_PARAMETER;
    Name = WmipAnsiToWide(InstanceName);
    if (Name == NULL)
        return ERROR_NOT_ENOUGH_MEMORY;
    Error = WmiSetSingleInstanceW(DataBlockHandle, Name, Reserved, ValueBufferSize, ValueBuffer);
    HeapFree(GetProcessHeap(), 0, Name);
    return Error;
}

ULONG
WMIAPI
WmiSetSingleItemA(
    _In_ WMIHANDLE DataBlockHandle,
    _In_ LPCSTR InstanceName,
    _In_ ULONG DataItemId,
    _In_ ULONG Reserved,
    _In_ ULONG ValueBufferSize,
    _In_reads_bytes_(ValueBufferSize) PVOID ValueBuffer)
{
    LPWSTR Name;
    ULONG Error;

    if (InstanceName == NULL)
        return ERROR_INVALID_PARAMETER;
    Name = WmipAnsiToWide(InstanceName);
    if (Name == NULL)
        return ERROR_NOT_ENOUGH_MEMORY;
    Error = WmiSetSingleItemW(DataBlockHandle, Name, DataItemId, Reserved, ValueBufferSize, ValueBuffer);
    HeapFree(GetProcessHeap(), 0, Name);
    return Error;
}

ULONG
WMIAPI
WmiExecuteMethodA(
    _In_ WMIHANDLE MethodDataBlockHandle,
    _In_ LPCSTR MethodInstanceName,
    _In_ ULONG MethodId,
    _In_ ULONG InputValueBufferSize,
    _In_reads_bytes_opt_(InputValueBufferSize) PVOID InputValueBuffer,
    _Inout_opt_ ULONG *OutputBufferSize,
    _Out_writes_bytes_opt_(*OutputBufferSize) PVOID OutputBuffer)
{
    LPWSTR Name;
    ULONG Error;

    if (MethodInstanceName == NULL)
        return ERROR_INVALID_PARAMETER;
    Name = WmipAnsiToWide(MethodInstanceName);
    if (Name == NULL)
        return ERROR_NOT_ENOUGH_MEMORY;
    Error = WmiExecuteMethodW(MethodDataBlockHandle, Name, MethodId, InputValueBufferSize, InputValueBuffer,
                              OutputBufferSize, OutputBuffer);
    HeapFree(GetProcessHeap(), 0, Name);
    return Error;
}
