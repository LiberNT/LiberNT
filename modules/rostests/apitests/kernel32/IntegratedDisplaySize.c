/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test GetIntegratedDisplaySize
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"
#include <math.h>
#include <wingdi.h>
#include <initguid.h>
#include <wmium.h>
#include <wmidata.h>

typedef HRESULT (WINAPI *PFN_GET_INTEGRATED_DISPLAY_SIZE)(double *);
typedef ULONG (WINAPI *PFN_WMI_OPEN_BLOCK)(GUID *, ULONG, WMIHANDLE *);
typedef ULONG (WINAPI *PFN_WMI_CLOSE_BLOCK)(WMIHANDLE);
typedef ULONG (WINAPI *PFN_WMI_QUERY_ALL_DATA_W)(WMIHANDLE, ULONG *, LPVOID);
typedef ULONG (WINAPI *PFN_WMI_QUERY_SINGLE_INSTANCE_W)(WMIHANDLE, LPCWSTR, PULONG, LPVOID);

static PFN_WMI_OPEN_BLOCK pWmiOpenBlock;
static PFN_WMI_CLOSE_BLOCK pWmiCloseBlock;
static PFN_WMI_QUERY_ALL_DATA_W pWmiQueryAllDataW;
static PFN_WMI_QUERY_SINGLE_INSTANCE_W pWmiQuerySingleInstanceW;

static
PBYTE
QueryBlock(
    _In_ const GUID *Guid,
    _In_opt_ PCWSTR InstanceName,
    _Out_ PULONG Size)
{
    WMIHANDLE Handle;
    PBYTE Buffer = NULL;
    ULONG Error;

    *Size = 0;
    Error = pWmiOpenBlock((GUID *)Guid, WMIGUID_QUERY, &Handle);
    if (Error != ERROR_SUCCESS)
    {
        trace("WmiOpenBlock failed with %lu\n", Error);
        return NULL;
    }
    Error = InstanceName ? pWmiQuerySingleInstanceW(Handle, InstanceName, Size, NULL) :
                           pWmiQueryAllDataW(Handle, Size, NULL);
    if (Error == ERROR_INSUFFICIENT_BUFFER && *Size >= sizeof(WNODE_HEADER))
    {
        Buffer = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, *Size);
        if (Buffer)
        {
            Error = InstanceName ? pWmiQuerySingleInstanceW(Handle, InstanceName, Size, Buffer) :
                                   pWmiQueryAllDataW(Handle, Size, Buffer);
            if (Error != ERROR_SUCCESS)
            {
                HeapFree(GetProcessHeap(), 0, Buffer);
                Buffer = NULL;
            }
        }
    }
    else
    {
        trace("WMI query returned %lu, size %lu\n", Error, *Size);
    }
    pWmiCloseBlock(Handle);
    return Buffer;
}

static
BOOL
IsIntegrated(
    _In_ ULONG Technology)
{
    return Technology == (ULONG)DISPLAYCONFIG_OUTPUT_TECHNOLOGY_INTERNAL ||
           Technology == (ULONG)DISPLAYCONFIG_OUTPUT_TECHNOLOGY_DISPLAYPORT_EMBEDDED ||
           Technology == (ULONG)DISPLAYCONFIG_OUTPUT_TECHNOLOGY_UDI_EMBEDDED;
}

static
BOOL
ExpectedDiagonal(
    _In_ PCWSTR InstanceName,
    _Out_ double *Diagonal)
{
    const WNODE_SINGLE_INSTANCE *Node;
    const WmiMonitorBasicDisplayParams *Params;
    PBYTE Buffer;
    ULONG Size;
    BOOL Found = FALSE;

    Buffer = QueryBlock(&WmiMonitorBasicDisplayParams_GUID, InstanceName, &Size);
    if (!Buffer)
        return FALSE;
    Node = (const WNODE_SINGLE_INSTANCE *)Buffer;
    if (Size >= sizeof(*Node) && Node->DataBlockOffset <= Size &&
        Node->SizeDataBlock >= WmiMonitorBasicDisplayParams_SIZE &&
        Node->SizeDataBlock <= Size - Node->DataBlockOffset)
    {
        Params = (const WmiMonitorBasicDisplayParams *)(Buffer + Node->DataBlockOffset);
        trace("Image size %u x %u cm\n", Params->MaxHorizontalImageSize, Params->MaxVerticalImageSize);
        if (Params->MaxHorizontalImageSize && Params->MaxVerticalImageSize)
        {
            *Diagonal = sqrt((double)Params->MaxHorizontalImageSize * Params->MaxHorizontalImageSize +
                             (double)Params->MaxVerticalImageSize * Params->MaxVerticalImageSize) / 2.54;
            Found = TRUE;
        }
    }
    HeapFree(GetProcessHeap(), 0, Buffer);
    return Found;
}

static
BOOL
FindIntegratedDisplay(
    _Out_ double *Diagonal)
{
    const WNODE_ALL_DATA *Node;
    PBYTE Buffer;
    ULONG Size, Offset = 0, Index, DataOffset, NameOffset;
    USHORT NameLength;
    WCHAR Name[MAX_PATH];
    ULONG Technology;
    BOOL Found = FALSE;

    Buffer = QueryBlock(&WmiMonitorConnectionParams_GUID, NULL, &Size);
    if (!Buffer)
        return FALSE;

    while (!Found && Size - Offset >= sizeof(WNODE_ALL_DATA))
    {
        Node = (const WNODE_ALL_DATA *)(Buffer + Offset);
        if (Node->WnodeHeader.BufferSize > Size - Offset)
            break;
        for (Index = 0; !Found && Index < Node->InstanceCount; Index++)
        {
            if (Node->WnodeHeader.Flags & WNODE_FLAG_FIXED_INSTANCE_SIZE)
                DataOffset = Node->DataBlockOffset + Index * ((Node->FixedInstanceSize + 7) & ~7);
            else
                DataOffset = Node->OffsetInstanceDataAndLength[Index].OffsetInstanceData;
            if (!Node->OffsetInstanceNameOffsets ||
                Node->OffsetInstanceNameOffsets + (Index + 1) * sizeof(ULONG) > Node->WnodeHeader.BufferSize)
            {
                break;
            }
            NameOffset = ((const ULONG *)((const BYTE *)Node + Node->OffsetInstanceNameOffsets))[Index];
            if (NameOffset + sizeof(USHORT) > Node->WnodeHeader.BufferSize)
                break;
            NameLength = *(const USHORT *)((const BYTE *)Node + NameOffset);
            if (DataOffset + sizeof(ULONG) > Node->WnodeHeader.BufferSize ||
                NameOffset + sizeof(USHORT) + NameLength > Node->WnodeHeader.BufferSize ||
                NameLength / sizeof(WCHAR) >= RTL_NUMBER_OF(Name))
            {
                break;
            }
            RtlCopyMemory(Name, (const BYTE *)Node + NameOffset + sizeof(USHORT), NameLength);
            Name[NameLength / sizeof(WCHAR)] = UNICODE_NULL;
            Technology = *(const ULONG *)((const BYTE *)Node + DataOffset);
            trace("Monitor '%S': output technology 0x%lx\n", Name, Technology);
            if (IsIntegrated(Technology))
                Found = ExpectedDiagonal(Name, Diagonal);
        }
        if (!Node->WnodeHeader.Linkage)
            break;
        Offset += Node->WnodeHeader.Linkage;
    }
    HeapFree(GetProcessHeap(), 0, Buffer);
    return Found;
}

START_TEST(IntegratedDisplaySize)
{
    PFN_GET_INTEGRATED_DISPLAY_SIZE pGetIntegratedDisplaySize;
    HMODULE Advapi32;
    double Expected = 0.0, Size;
    BOOL HasIntegrated;
    HRESULT hr;

    pGetIntegratedDisplaySize = (PFN_GET_INTEGRATED_DISPLAY_SIZE)GetProcAddress(GetModuleHandleW(L"kernelbase.dll"),
                                                                              "GetIntegratedDisplaySize");
    if (!pGetIntegratedDisplaySize)
    {
        skip("GetIntegratedDisplaySize is not available\n");
        return;
    }

    Advapi32 = LoadLibraryW(L"advapi32.dll");
    ok(Advapi32 != NULL, "advapi32.dll did not load\n");
    if (!Advapi32)
        return;
    pWmiOpenBlock = (PFN_WMI_OPEN_BLOCK)GetProcAddress(Advapi32, "WmiOpenBlock");
    pWmiCloseBlock = (PFN_WMI_CLOSE_BLOCK)GetProcAddress(Advapi32, "WmiCloseBlock");
    pWmiQueryAllDataW = (PFN_WMI_QUERY_ALL_DATA_W)GetProcAddress(Advapi32, "WmiQueryAllDataW");
    pWmiQuerySingleInstanceW = (PFN_WMI_QUERY_SINGLE_INSTANCE_W)GetProcAddress(Advapi32, "WmiQuerySingleInstanceW");
    if (!pWmiOpenBlock || !pWmiCloseBlock || !pWmiQueryAllDataW || !pWmiQuerySingleInstanceW)
    {
        skip("The WMI data block functions are not available\n");
        FreeLibrary(Advapi32);
        return;
    }

    HasIntegrated = FindIntegratedDisplay(&Expected);
    trace("Integrated display %s, expected diagonal %.2f\n", HasIntegrated ? "found" : "not found", Expected);

    Size = -1.0;
    hr = pGetIntegratedDisplaySize(&Size);
    if (!HasIntegrated)
    {
        ok(FAILED(hr), "GetIntegratedDisplaySize succeeded without an integrated display: 0x%lx\n", hr);
        ok_hex(hr, HRESULT_FROM_WIN32(ERROR_NOT_FOUND));
        ok(Size == -1.0, "Size was written: %f\n", Size);
    }
    else
    {
        ok_hex(hr, S_OK);
        ok(fabs(Size - Expected) < 1.0, "Size %f, expected about %f\n", Size, Expected);
    }

    FreeLibrary(Advapi32);
}
