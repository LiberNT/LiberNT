/*
 * PROJECT:     LiberNT Setup
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Physical disk names from the WMI storage provider
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "libernt.h"

#define COBJMACROS
#include <initguid.h>
#include <objbase.h>
#include <wbemcli.h>

typedef struct _SETUP_DISK_INFO
{
    ULONG DiskNumber;
    LONG BusType;
    LONG MediaType;
    WCHAR FriendlyName[96];
} SETUP_DISK_INFO, *PSETUP_DISK_INFO;

static PSETUP_DISK_INFO DiskInfos;
static ULONG DiskInfoCount;

static PCWSTR
GetBusTypeName(
    _In_ LONG BusType)
{
    static const PCWSTR Names[] =
    {
        NULL, L"SCSI", L"ATAPI", L"ATA", L"1394", L"SSA", L"Fibre Channel", L"USB", L"RAID",
        L"iSCSI", L"SAS", L"SATA", L"SD", L"MMC", L"Virtual", L"File Backed Virtual",
        L"Storage Spaces", L"NVMe"
    };

    if (BusType <= 0 || BusType >= (LONG)ARRAYSIZE(Names))
        return NULL;
    return Names[BusType];
}

static PCWSTR
GetMediaTypeName(
    _In_ LONG MediaType)
{
    switch (MediaType)
    {
        case 3:
            return L"HDD";
        case 4:
            return L"SSD";
        case 5:
            return L"SCM";
        default:
            return NULL;
    }
}

static LONG
GetIntegerProperty(
    _In_ IWbemClassObject* Object,
    _In_ PCWSTR Name)
{
    VARIANT Value;
    LONG Result = 0;

    VariantInit(&Value);
    if (SUCCEEDED(IWbemClassObject_Get(Object, Name, 0, &Value, NULL, NULL)))
    {
        if (V_VT(&Value) == VT_I4)
            Result = V_I4(&Value);
        else if (V_VT(&Value) == VT_UI2 || V_VT(&Value) == VT_I2)
            Result = V_I2(&Value);
        VariantClear(&Value);
    }
    return Result;
}

static BOOL
GetStringProperty(
    _In_ IWbemClassObject* Object,
    _In_ PCWSTR Name,
    _Out_writes_(cchText) PWSTR Text,
    _In_ SIZE_T cchText)
{
    VARIANT Value;
    BOOL Success = FALSE;

    Text[0] = UNICODE_NULL;
    VariantInit(&Value);
    if (SUCCEEDED(IWbemClassObject_Get(Object, Name, 0, &Value, NULL, NULL)))
    {
        if (V_VT(&Value) == VT_BSTR && V_BSTR(&Value))
            Success = SUCCEEDED(StringCchCopyW(Text, cchText, V_BSTR(&Value)));
        VariantClear(&Value);
    }
    return Success;
}

VOID
DiskInfoFree(VOID)
{
    if (DiskInfos)
        HeapFree(ProcessHeap, 0, DiskInfos);
    DiskInfos = NULL;
    DiskInfoCount = 0;
}

VOID
DiskInfoRefresh(VOID)
{
    IWbemLocator* Locator = NULL;
    IWbemServices* Services = NULL;
    IEnumWbemClassObject* Enum = NULL;
    IWbemClassObject* Object;
    BSTR Namespace = NULL, Language = NULL, Query = NULL;
    WCHAR DeviceId[16];
    ULONG Returned;
    HRESULT hr;

    DiskInfoFree();

    hr = CoCreateInstance(&CLSID_WbemLocator, NULL, CLSCTX_INPROC_SERVER, &IID_IWbemLocator, (PVOID*)&Locator);
    if (FAILED(hr))
        return;

    Namespace = SysAllocString(L"ROOT\\Microsoft\\Windows\\Storage");
    Language = SysAllocString(L"WQL");
    Query = SysAllocString(L"SELECT DeviceId, FriendlyName, BusType, MediaType FROM MSFT_PhysicalDisk");
    if (!Namespace || !Language || !Query)
        goto Quit;

    hr = IWbemLocator_ConnectServer(Locator, Namespace, NULL, NULL, NULL, 0, NULL, NULL, &Services);
    if (FAILED(hr))
        goto Quit;

    hr = IWbemServices_ExecQuery(Services, Language, Query,
                                 WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY,
                                 NULL, &Enum);
    if (FAILED(hr))
        goto Quit;

    while (IEnumWbemClassObject_Next(Enum, WBEM_INFINITE, 1, &Object, &Returned) == S_OK && Returned == 1)
    {
        PSETUP_DISK_INFO Infos;
        SETUP_DISK_INFO Info;

        ZeroMemory(&Info, sizeof(Info));
        if (GetStringProperty(Object, L"DeviceId", DeviceId, ARRAYSIZE(DeviceId)) &&
            GetStringProperty(Object, L"FriendlyName", Info.FriendlyName, ARRAYSIZE(Info.FriendlyName)))
        {
            Info.DiskNumber = wcstoul(DeviceId, NULL, 10);
            Info.BusType = GetIntegerProperty(Object, L"BusType");
            Info.MediaType = GetIntegerProperty(Object, L"MediaType");

            if (DiskInfos)
                Infos = HeapReAlloc(ProcessHeap, 0, DiskInfos, (DiskInfoCount + 1) * sizeof(Info));
            else
                Infos = HeapAlloc(ProcessHeap, 0, sizeof(Info));
            if (Infos)
            {
                Infos[DiskInfoCount++] = Info;
                DiskInfos = Infos;
            }
        }
        IWbemClassObject_Release(Object);
    }

Quit:
    if (Enum)
        IEnumWbemClassObject_Release(Enum);
    if (Services)
        IWbemServices_Release(Services);
    IWbemLocator_Release(Locator);
    SysFreeString(Query);
    SysFreeString(Language);
    SysFreeString(Namespace);
}

BOOL
DiskInfoGetDescription(
    _In_ ULONG DiskNumber,
    _Out_writes_(cchText) PWSTR Text,
    _In_ SIZE_T cchText)
{
    ULONG Index;

    for (Index = 0; Index < DiskInfoCount; Index++)
    {
        PSETUP_DISK_INFO Info = &DiskInfos[Index];
        PCWSTR Bus, Media;

        if (Info->DiskNumber != DiskNumber || !Info->FriendlyName[0])
            continue;

        StringCchCopyW(Text, cchText, Info->FriendlyName);
        Bus = GetBusTypeName(Info->BusType);
        Media = GetMediaTypeName(Info->MediaType);
        if (Bus)
        {
            StringCchCatW(Text, cchText, L" \x00B7 ");
            StringCchCatW(Text, cchText, Bus);
        }
        if (Media)
        {
            StringCchCatW(Text, cchText, L" \x00B7 ");
            StringCchCatW(Text, cchText, Media);
        }
        return TRUE;
    }

    return FALSE;
}
