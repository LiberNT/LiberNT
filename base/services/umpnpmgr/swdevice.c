/*
 * PROJECT:     LiberNT Plug and Play Manager
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Software device enumeration
 * COPYRIGHT:   Copyright 2026 mikuta0407 <mikuta0407@mikuta0407.net>
 */

#include "precomp.h"
#include <swdevicedef.h>

#define NDEBUG
#include <debug.h>

#define SWD_ENUMERATOR L"SWD"
#define SW_DEVICE_CAPABILITIES_ALL (SWDeviceCapabilitiesRemovable | SWDeviceCapabilitiesSilentInstall | \
                                    SWDeviceCapabilitiesNoDisplayInUI | SWDeviceCapabilitiesDriverRequired)

typedef struct _SW_DEVICE_ENTRY
{
    LIST_ENTRY ListEntry;
    BOOL Open;
    DWORD Lifetime;
    WCHAR DeviceId[MAX_DEVICE_ID_LEN];
} SW_DEVICE_ENTRY, *PSW_DEVICE_ENTRY;

static LIST_ENTRY SwDeviceListHead = { &SwDeviceListHead, &SwDeviceListHead };
static SRWLOCK SwDeviceLock = SRWLOCK_INIT;

static
BOOL
IsValidIdPart(
    _In_ PCWSTR Part)
{
    PCWSTR p;

    if (!*Part)
        return FALSE;
    for (p = Part; *p; p++)
    {
        if (*p <= L' ' || *p > 0x7F || *p == L'\\' || *p == L',')
            return FALSE;
    }
    return TRUE;
}

static
BOOL
GetMultiSzLength(
    _In_reads_opt_(Length) PCWSTR List,
    _In_ DWORD Length,
    _Out_ PDWORD Used)
{
    DWORD i = 0;

    *Used = 0;
    if (!List)
        return Length == 0;
    while (i < Length)
    {
        if (!List[i])
        {
            *Used = i + 1;
            return TRUE;
        }
        while (i < Length && List[i])
            i++;
        if (i == Length)
            return FALSE;
        i++;
    }
    return FALSE;
}

static
PSW_DEVICE_ENTRY
FindSwDevice(
    _In_ PCWSTR DeviceId)
{
    PLIST_ENTRY Entry;

    for (Entry = SwDeviceListHead.Flink; Entry != &SwDeviceListHead; Entry = Entry->Flink)
    {
        PSW_DEVICE_ENTRY Device = CONTAINING_RECORD(Entry, SW_DEVICE_ENTRY, ListEntry);
        if (!_wcsicmp(Device->DeviceId, DeviceId))
            return Device;
    }
    return NULL;
}

static
NTSTATUS
EnumerateRootDevice(VOID)
{
    PLUGPLAY_CONTROL_ENUMERATE_DEVICE_DATA Data;

    RtlInitUnicodeString(&Data.DeviceInstance, L"HTREE\\ROOT\\0");
    Data.Flags = 0;
    return NtPlugPlayControl(PlugPlayControlEnumerateDevice, &Data, sizeof(Data));
}

static
LONG
SetDeviceReported(
    _In_ PCWSTR DeviceId,
    _In_ BOOL Reported)
{
    WCHAR KeyName[MAX_DEVICE_ID_LEN + 16];
    HKEY hControl;
    DWORD Value = 1;
    LONG Error;

    swprintf(KeyName, ARRAYSIZE(KeyName), L"%s\\Control", DeviceId);
    if (!Reported)
    {
        Error = RegOpenKeyExW(hEnumKey, KeyName, 0, KEY_SET_VALUE, &hControl);
        if (Error != ERROR_SUCCESS)
            return Error;
        Error = RegDeleteValueW(hControl, L"DeviceReported");
        RegCloseKey(hControl);
        return Error;
    }

    Error = RegCreateKeyExW(hEnumKey, KeyName, 0, NULL, REG_OPTION_VOLATILE,
                            KEY_SET_VALUE, NULL, &hControl, NULL);
    if (Error != ERROR_SUCCESS)
        return Error;
    Error = RegSetValueExW(hControl, L"DeviceReported", 0, REG_DWORD, (PBYTE)&Value, sizeof(Value));
    RegCloseKey(hControl);
    return Error;
}

static
LONG
WriteDeviceInstance(
    _In_ PCWSTR DeviceId,
    _In_reads_opt_(HardwareIdsLength) PCWSTR HardwareIds,
    _In_ DWORD HardwareIdsLength,
    _In_reads_opt_(CompatibleIdsLength) PCWSTR CompatibleIds,
    _In_ DWORD CompatibleIdsLength,
    _In_ DWORD CapabilityFlags,
    _In_opt_ PCWSTR DeviceDescription,
    _In_opt_ PCWSTR DeviceLocation)
{
    static const WCHAR Generic[] = L"SWD\\GenericRaw\0SWD\\Generic\0";
    PCWSTR GenericIds = (CapabilityFlags & SWDeviceCapabilitiesDriverRequired) ?
                        Generic + ARRAYSIZE(L"SWD\\GenericRaw") : Generic;
    DWORD GenericLength = ARRAYSIZE(Generic) - (DWORD)(GenericIds - Generic);
    DWORD Capabilities = CM_DEVCAP_UNIQUEID | CM_DEVCAP_SURPRISEREMOVALOK;
    PWSTR Compatible;
    HKEY hInstance;
    LONG Error;

    Error = RegCreateKeyExW(hEnumKey, DeviceId, 0, NULL, REG_OPTION_NON_VOLATILE,
                            KEY_ALL_ACCESS, NULL, &hInstance, NULL);
    if (Error != ERROR_SUCCESS)
        return Error;

    if (HardwareIdsLength > 1)
        Error = RegSetValueExW(hInstance, L"HardwareID", 0, REG_MULTI_SZ,
                               (PBYTE)HardwareIds, HardwareIdsLength * sizeof(WCHAR));
    else
        RegDeleteValueW(hInstance, L"HardwareID");

    if (Error == ERROR_SUCCESS)
    {
        DWORD Own = CompatibleIdsLength > 1 ? CompatibleIdsLength - 1 : 0;

        Compatible = HeapAlloc(GetProcessHeap(), 0, (Own + GenericLength) * sizeof(WCHAR));
        if (!Compatible)
        {
            Error = ERROR_NOT_ENOUGH_MEMORY;
        }
        else
        {
            if (Own)
                CopyMemory(Compatible, CompatibleIds, Own * sizeof(WCHAR));
            CopyMemory(Compatible + Own, GenericIds, GenericLength * sizeof(WCHAR));
            Error = RegSetValueExW(hInstance, L"CompatibleIDs", 0, REG_MULTI_SZ,
                                   (PBYTE)Compatible, (Own + GenericLength) * sizeof(WCHAR));
            HeapFree(GetProcessHeap(), 0, Compatible);
        }
    }

    if (Error == ERROR_SUCCESS)
    {
        if (DeviceDescription)
            Error = RegSetValueExW(hInstance, L"DeviceDesc", 0, REG_SZ, (PBYTE)DeviceDescription,
                                   (DWORD)(wcslen(DeviceDescription) + 1) * sizeof(WCHAR));
        else
            RegDeleteValueW(hInstance, L"DeviceDesc");
    }

    if (Error == ERROR_SUCCESS)
    {
        if (DeviceLocation)
            Error = RegSetValueExW(hInstance, L"LocationInformation", 0, REG_SZ, (PBYTE)DeviceLocation,
                                   (DWORD)(wcslen(DeviceLocation) + 1) * sizeof(WCHAR));
        else
            RegDeleteValueW(hInstance, L"LocationInformation");
    }

    if (Error == ERROR_SUCCESS)
    {
        if (CapabilityFlags & SWDeviceCapabilitiesRemovable)
            Capabilities |= CM_DEVCAP_REMOVABLE;
        if (CapabilityFlags & SWDeviceCapabilitiesSilentInstall)
            Capabilities |= CM_DEVCAP_SILENTINSTALL;
        if (!(CapabilityFlags & SWDeviceCapabilitiesDriverRequired))
            Capabilities |= CM_DEVCAP_RAWDEVICEOK;
        /* TODO: SWDeviceCapabilitiesNoDisplayInUI */
        Error = RegSetValueExW(hInstance, L"Capabilities", 0, REG_DWORD,
                               (PBYTE)&Capabilities, sizeof(Capabilities));
    }

    RegCloseKey(hInstance);
    return Error;
}

static
VOID
ReleaseSwDevice(
    _In_ PSW_DEVICE_ENTRY Device)
{
    BOOL Remove;

    AcquireSRWLockExclusive(&SwDeviceLock);
    Device->Open = FALSE;
    Remove = Device->Lifetime == SWDeviceLifetimeHandle;
    if (Remove)
        RemoveEntryList(&Device->ListEntry);
    ReleaseSRWLockExclusive(&SwDeviceLock);

    if (!Remove)
        return;

    DPRINT("Removing software device %S\n", Device->DeviceId);
    SetDeviceReported(Device->DeviceId, FALSE);
    EnumerateRootDevice();
    HeapFree(GetProcessHeap(), 0, Device);
}

VOID
__RPC_USER
PNP_SWDEVICE_HANDLE_rundown(
    PNP_SWDEVICE_HANDLE hSwDevice)
{
    if (hSwDevice)
        ReleaseSwDevice((PSW_DEVICE_ENTRY)hSwDevice);
}

HRESULT
__stdcall
PNP_SwDeviceCreate(
    handle_t hBinding,
    LPWSTR pszEnumeratorName,
    LPWSTR pszParentDeviceInstance,
    LPWSTR pszInstanceId,
    WCHAR *pszzHardwareIds,
    PNPSW_ID_LIST_LEN ulHardwareIdsLength,
    WCHAR *pszzCompatibleIds,
    PNPSW_ID_LIST_LEN ulCompatibleIdsLength,
    DWORD ulCapabilityFlags,
    LPWSTR pszDeviceDescription,
    LPWSTR pszDeviceLocation,
    WCHAR szDeviceInstanceId[PNPSW_MAX_DEVICE_ID_LEN],
    PPNP_SWDEVICE_HANDLE phSwDevice)
{
    WCHAR DeviceId[MAX_DEVICE_ID_LEN];
    PSW_DEVICE_ENTRY Device, New;
    DWORD HardwareLength, CompatibleLength;
    LONG Error;
    NTSTATUS Status;
    HRESULT hr = S_OK;

    UNREFERENCED_PARAMETER(hBinding);

    DPRINT("PNP_SwDeviceCreate(%S %S %S)\n", pszEnumeratorName, pszParentDeviceInstance, pszInstanceId);

    *phSwDevice = NULL;
    szDeviceInstanceId[0] = UNICODE_NULL;

    if (!IsValidIdPart(pszEnumeratorName) || !IsValidIdPart(pszInstanceId) ||
        (ulCapabilityFlags & ~SW_DEVICE_CAPABILITIES_ALL) ||
        !GetMultiSzLength(pszzHardwareIds, ulHardwareIdsLength, &HardwareLength) ||
        !GetMultiSzLength(pszzCompatibleIds, ulCompatibleIdsLength, &CompatibleLength))
    {
        return E_INVALIDARG;
    }
    if ((ulCapabilityFlags & SWDeviceCapabilitiesDriverRequired) &&
        HardwareLength <= 1 && CompatibleLength <= 1)
    {
        return E_INVALIDARG;
    }

    /* TODO: software devices below a parent other than the root device */
    if (!IsRootDeviceInstanceID(pszParentDeviceInstance))
        return HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED);

    if (_snwprintf(DeviceId, ARRAYSIZE(DeviceId), SWD_ENUMERATOR L"\\%s\\%s",
                   pszEnumeratorName, pszInstanceId) < 0)
    {
        return E_INVALIDARG;
    }
    DeviceId[ARRAYSIZE(DeviceId) - 1] = UNICODE_NULL;

    New = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*New));
    if (!New)
        return E_OUTOFMEMORY;

    AcquireSRWLockExclusive(&SwDeviceLock);
    Device = FindSwDevice(DeviceId);
    if (Device)
    {
        if (Device->Open)
        {
            hr = HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS);
        }
        else
        {
            Device->Open = TRUE;
            wcscpy(szDeviceInstanceId, Device->DeviceId);
            *phSwDevice = (PNP_SWDEVICE_HANDLE)Device;
        }
        ReleaseSRWLockExclusive(&SwDeviceLock);
        HeapFree(GetProcessHeap(), 0, New);
        return hr;
    }

    Error = WriteDeviceInstance(DeviceId,
                                pszzHardwareIds, HardwareLength,
                                pszzCompatibleIds, CompatibleLength,
                                ulCapabilityFlags,
                                pszDeviceDescription,
                                pszDeviceLocation);
    if (Error == ERROR_SUCCESS)
        Error = SetDeviceReported(DeviceId, TRUE);
    if (Error != ERROR_SUCCESS)
    {
        ReleaseSRWLockExclusive(&SwDeviceLock);
        HeapFree(GetProcessHeap(), 0, New);
        return HRESULT_FROM_WIN32(Error);
    }

    SetDeviceDoNotInstall(DeviceId, FALSE);
    Status = EnumerateRootDevice();
    if (!NT_SUCCESS(Status) || !IsPresentDeviceInstanceID(DeviceId))
    {
        DPRINT1("Software device %S was not enumerated (Status 0x%08lx)\n", DeviceId, Status);
        SetDeviceReported(DeviceId, FALSE);
        ReleaseSRWLockExclusive(&SwDeviceLock);
        EnumerateRootDevice();
        HeapFree(GetProcessHeap(), 0, New);
        return NT_SUCCESS(Status) ? E_FAIL : HRESULT_FROM_NT(Status);
    }

    New->Open = TRUE;
    New->Lifetime = SWDeviceLifetimeHandle;
    wcscpy(New->DeviceId, DeviceId);
    InsertTailList(&SwDeviceListHead, &New->ListEntry);
    ReleaseSRWLockExclusive(&SwDeviceLock);

    wcscpy(szDeviceInstanceId, DeviceId);
    *phSwDevice = (PNP_SWDEVICE_HANDLE)New;
    return S_OK;
}

HRESULT
__stdcall
PNP_SwDeviceClose(
    PPNP_SWDEVICE_HANDLE phSwDevice)
{
    PSW_DEVICE_ENTRY Device = (PSW_DEVICE_ENTRY)*phSwDevice;

    if (!Device)
        return E_HANDLE;

    *phSwDevice = NULL;
    ReleaseSwDevice(Device);
    return S_OK;
}

HRESULT
__stdcall
PNP_SwDeviceSetLifetime(
    PNP_SWDEVICE_HANDLE hSwDevice,
    DWORD ulLifetime)
{
    PSW_DEVICE_ENTRY Device = (PSW_DEVICE_ENTRY)hSwDevice;

    if (!Device)
        return E_HANDLE;
    if (ulLifetime >= SWDeviceLifetimeMax)
        return E_INVALIDARG;

    /* TODO: enumerate SWDeviceLifetimeParentPresent devices again after a reboot */
    AcquireSRWLockExclusive(&SwDeviceLock);
    Device->Lifetime = ulLifetime;
    ReleaseSRWLockExclusive(&SwDeviceLock);
    return S_OK;
}

HRESULT
__stdcall
PNP_SwDeviceGetLifetime(
    PNP_SWDEVICE_HANDLE hSwDevice,
    DWORD *pulLifetime)
{
    PSW_DEVICE_ENTRY Device = (PSW_DEVICE_ENTRY)hSwDevice;

    if (!Device)
        return E_HANDLE;

    AcquireSRWLockShared(&SwDeviceLock);
    *pulLifetime = Device->Lifetime;
    ReleaseSRWLockShared(&SwDeviceLock);
    return S_OK;
}
