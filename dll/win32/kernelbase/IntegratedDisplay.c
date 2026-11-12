/*
 * PROJECT:     LiberNT KernelBase
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Diagonal size of the integrated display
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <windows.h>
#include <math.h>
#include <initguid.h>
#include <wmium.h>
#include <wmidata.h>

typedef ULONG (WINAPI *PFN_WMI_OPEN_BLOCK)(GUID *, ULONG, WMIHANDLE *);
typedef ULONG (WINAPI *PFN_WMI_CLOSE_BLOCK)(WMIHANDLE);
typedef ULONG (WINAPI *PFN_WMI_QUERY_ALL_DATA_W)(WMIHANDLE, ULONG *, LPVOID);
typedef ULONG (WINAPI *PFN_WMI_QUERY_SINGLE_INSTANCE_W)(WMIHANDLE, LPCWSTR, PULONG, LPVOID);

typedef struct _DISPLAY_WMI_API
{
    HMODULE Module;
    PFN_WMI_OPEN_BLOCK OpenBlock;
    PFN_WMI_CLOSE_BLOCK CloseBlock;
    PFN_WMI_QUERY_ALL_DATA_W QueryAllData;
    PFN_WMI_QUERY_SINGLE_INSTANCE_W QuerySingleInstance;
} DISPLAY_WMI_API, *PDISPLAY_WMI_API;

static
BOOL
DisplayLoadWmiApi(
    _Out_ PDISPLAY_WMI_API Api)
{
    Api->Module = LoadLibraryW(L"advapi32.dll");
    if (Api->Module == NULL)
        return FALSE;

    Api->OpenBlock = (PFN_WMI_OPEN_BLOCK)GetProcAddress(Api->Module, "WmiOpenBlock");
    Api->CloseBlock = (PFN_WMI_CLOSE_BLOCK)GetProcAddress(Api->Module, "WmiCloseBlock");
    Api->QueryAllData = (PFN_WMI_QUERY_ALL_DATA_W)GetProcAddress(Api->Module, "WmiQueryAllDataW");
    Api->QuerySingleInstance = (PFN_WMI_QUERY_SINGLE_INSTANCE_W)GetProcAddress(Api->Module, "WmiQuerySingleInstanceW");
    if (Api->OpenBlock == NULL || Api->CloseBlock == NULL ||
        Api->QueryAllData == NULL || Api->QuerySingleInstance == NULL)
    {
        FreeLibrary(Api->Module);
        return FALSE;
    }
    return TRUE;
}

static
PVOID
DisplayQueryBlock(
    _In_ PDISPLAY_WMI_API Api,
    _In_ const GUID *Guid,
    _In_opt_ PCWSTR InstanceName,
    _Out_ PULONG BufferSize)
{
    WMIHANDLE Handle;
    PVOID Buffer = NULL;
    ULONG Size = 0, Error;

    *BufferSize = 0;
    if (Api->OpenBlock((GUID *)Guid, WMIGUID_QUERY, &Handle) != ERROR_SUCCESS)
        return NULL;

    if (InstanceName != NULL)
        Error = Api->QuerySingleInstance(Handle, InstanceName, &Size, NULL);
    else
        Error = Api->QueryAllData(Handle, &Size, NULL);

    if (Error == ERROR_INSUFFICIENT_BUFFER && Size >= sizeof(WNODE_HEADER))
    {
        Buffer = HeapAlloc(GetProcessHeap(), 0, Size);
        if (Buffer != NULL)
        {
            if (InstanceName != NULL)
                Error = Api->QuerySingleInstance(Handle, InstanceName, &Size, Buffer);
            else
                Error = Api->QueryAllData(Handle, &Size, Buffer);
            if (Error != ERROR_SUCCESS || Size < sizeof(WNODE_HEADER))
            {
                HeapFree(GetProcessHeap(), 0, Buffer);
                Buffer = NULL;
            }
        }
    }

    Api->CloseBlock(Handle);
    if (Buffer != NULL)
        *BufferSize = Size;
    return Buffer;
}

static
const BYTE *
DisplaySingleInstanceData(
    _In_reads_bytes_(Size) const BYTE *Buffer,
    _In_ ULONG Size,
    _In_ ULONG MinimumSize,
    _Out_ PULONG DataSize)
{
    const WNODE_SINGLE_INSTANCE *Node = (const WNODE_SINGLE_INSTANCE *)Buffer;

    *DataSize = 0;
    if (Size < sizeof(*Node) || Node->WnodeHeader.BufferSize > Size ||
        Node->DataBlockOffset > Node->WnodeHeader.BufferSize ||
        Node->SizeDataBlock > Node->WnodeHeader.BufferSize - Node->DataBlockOffset ||
        Node->SizeDataBlock < MinimumSize)
    {
        return NULL;
    }
    *DataSize = Node->SizeDataBlock;
    return Buffer + Node->DataBlockOffset;
}

static
BOOL
DisplayAllDataInstance(
    _In_ const WNODE_ALL_DATA *Node,
    _In_ ULONG Index,
    _Out_ const BYTE **Data,
    _Out_ PULONG DataSize,
    _Out_writes_(NameChars) PWSTR Name,
    _In_ ULONG NameChars)
{
    ULONG Total = Node->WnodeHeader.BufferSize, DataOffset, NameOffset;
    USHORT NameLength;

    if (Node->WnodeHeader.Flags & WNODE_FLAG_FIXED_INSTANCE_SIZE)
    {
        DataOffset = Node->DataBlockOffset + Index * ((Node->FixedInstanceSize + 7) & ~7);
        *DataSize = Node->FixedInstanceSize;
    }
    else
    {
        if (FIELD_OFFSET(WNODE_ALL_DATA, OffsetInstanceDataAndLength) +
            (Index + 1) * sizeof(OFFSETINSTANCEDATAANDLENGTH) > Total)
        {
            return FALSE;
        }
        DataOffset = Node->OffsetInstanceDataAndLength[Index].OffsetInstanceData;
        *DataSize = Node->OffsetInstanceDataAndLength[Index].LengthInstanceData;
    }
    if (DataOffset > Total || *DataSize > Total - DataOffset)
        return FALSE;

    if (Node->OffsetInstanceNameOffsets == 0 || Node->OffsetInstanceNameOffsets > Total ||
        (Index + 1) * sizeof(ULONG) > Total - Node->OffsetInstanceNameOffsets)
    {
        return FALSE;
    }
    NameOffset = ((const ULONG *)((const BYTE *)Node + Node->OffsetInstanceNameOffsets))[Index];
    if (NameOffset > Total - sizeof(USHORT))
        return FALSE;
    NameLength = *(const USHORT *)((const BYTE *)Node + NameOffset);
    if (NameLength > Total - NameOffset - sizeof(USHORT) || NameLength / sizeof(WCHAR) >= NameChars)
        return FALSE;

    RtlCopyMemory(Name, (const BYTE *)Node + NameOffset + sizeof(USHORT), NameLength);
    Name[NameLength / sizeof(WCHAR)] = UNICODE_NULL;
    *Data = (const BYTE *)Node + DataOffset;
    return TRUE;
}

static
BOOL
DisplayIsIntegrated(
    _In_ ULONG VideoOutputTechnology)
{
    return VideoOutputTechnology == (ULONG)DISPLAYCONFIG_OUTPUT_TECHNOLOGY_INTERNAL ||
           VideoOutputTechnology == (ULONG)DISPLAYCONFIG_OUTPUT_TECHNOLOGY_DISPLAYPORT_EMBEDDED ||
           VideoOutputTechnology == (ULONG)DISPLAYCONFIG_OUTPUT_TECHNOLOGY_UDI_EMBEDDED;
}

static
BOOL
DisplayImageSize(
    _In_ PDISPLAY_WMI_API Api,
    _In_ PCWSTR InstanceName,
    _Out_ double *WidthMm,
    _Out_ double *HeightMm)
{
    const WmiMonitorBasicDisplayParams *Basic = NULL;
    const WmiMonitorListedSupportedSourceModes *Modes;
    const VideoModeDescriptor *Preferred = NULL;
    PBYTE BasicBuffer, ModesBuffer;
    ULONG Size, DataSize;
    BOOL Found = FALSE;

    BasicBuffer = DisplayQueryBlock(Api, &WmiMonitorBasicDisplayParams_GUID, InstanceName, &Size);
    if (BasicBuffer != NULL)
        Basic = (const WmiMonitorBasicDisplayParams *)DisplaySingleInstanceData(BasicBuffer, Size,
                                                                               WmiMonitorBasicDisplayParams_SIZE,
                                                                               &DataSize);

    ModesBuffer = DisplayQueryBlock(Api, &WmiMonitorListedSupportedSourceModes_GUID, InstanceName, &Size);
    if (ModesBuffer != NULL)
    {
        Modes = (const WmiMonitorListedSupportedSourceModes *)
            DisplaySingleInstanceData(ModesBuffer, Size,
                                      FIELD_OFFSET(WmiMonitorListedSupportedSourceModes, MonitorSourceModes),
                                      &DataSize);
        if (Modes != NULL && Modes->PreferredMonitorSourceModeIndex < Modes->NumOfMonitorSourceModes &&
            FIELD_OFFSET(WmiMonitorListedSupportedSourceModes, MonitorSourceModes) +
            (Modes->PreferredMonitorSourceModeIndex + 1) * sizeof(VideoModeDescriptor) <= DataSize)
        {
            Preferred = &Modes->MonitorSourceModes[Modes->PreferredMonitorSourceModeIndex];
        }
    }

    if (Basic != NULL && Basic->MaxHorizontalImageSize != 0 && Basic->MaxVerticalImageSize != 0)
    {
        *WidthMm = Basic->MaxHorizontalImageSize * 10.0;
        *HeightMm = Basic->MaxVerticalImageSize * 10.0;
        if (Preferred != NULL &&
            fabs(Preferred->HorizontalImageSize - *WidthMm) < 10.0 &&
            fabs(Preferred->VerticalImageSize - *HeightMm) < 10.0)
        {
            *WidthMm = Preferred->HorizontalImageSize;
            *HeightMm = Preferred->VerticalImageSize;
        }
        Found = TRUE;
    }
    else if (Preferred != NULL && Preferred->HorizontalImageSize >= 10 && Preferred->VerticalImageSize >= 10)
    {
        *WidthMm = Preferred->HorizontalImageSize;
        *HeightMm = Preferred->VerticalImageSize;
        Found = TRUE;
    }

    if (BasicBuffer != NULL)
        HeapFree(GetProcessHeap(), 0, BasicBuffer);
    if (ModesBuffer != NULL)
        HeapFree(GetProcessHeap(), 0, ModesBuffer);
    return Found;
}

static
BOOL
DisplayFindIntegrated(
    _In_ PDISPLAY_WMI_API Api,
    _Out_ double *Diagonal)
{
    const WNODE_ALL_DATA *Node;
    const BYTE *Data;
    PBYTE Buffer;
    WCHAR Name[MAX_PATH];
    ULONG Size, Offset = 0, Index, DataSize;
    double Width, Height;
    BOOL Found = FALSE;

    Buffer = DisplayQueryBlock(Api, &WmiMonitorConnectionParams_GUID, NULL, &Size);
    if (Buffer == NULL)
        return FALSE;

    while (!Found && Offset <= Size && Size - Offset >= sizeof(WNODE_ALL_DATA))
    {
        Node = (const WNODE_ALL_DATA *)(Buffer + Offset);
        if (Node->WnodeHeader.BufferSize > Size - Offset)
            break;

        for (Index = 0; !Found && Index < Node->InstanceCount; Index++)
        {
            if (!DisplayAllDataInstance(Node, Index, &Data, &DataSize, Name, RTL_NUMBER_OF(Name)))
                break;
            if (DataSize < WmiMonitorConnectionParams_SIZE ||
                !DisplayIsIntegrated(((const WmiMonitorConnectionParams *)Data)->VideoOutputTechnology))
            {
                continue;
            }
            if (DisplayImageSize(Api, Name, &Width, &Height))
            {
                *Diagonal = sqrt(Width * Width + Height * Height) / 25.4;
                Found = TRUE;
            }
        }

        if (Node->WnodeHeader.Linkage == 0)
            break;
        Offset += Node->WnodeHeader.Linkage;
    }

    HeapFree(GetProcessHeap(), 0, Buffer);
    return Found;
}

HRESULT
WINAPI
GetIntegratedDisplaySize(
    _Out_ double *sizeInInches)
{
    DISPLAY_WMI_API Api;
    double Diagonal = 0.0;
    BOOL Found;

    if (sizeInInches == NULL)
        return E_INVALIDARG;

    if (!DisplayLoadWmiApi(&Api))
        return HRESULT_FROM_WIN32(GetLastError());

    Found = DisplayFindIntegrated(&Api, &Diagonal);
    FreeLibrary(Api.Module);
    if (!Found)
        return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);

    *sizeInInches = Diagonal;
    return S_OK;
}
