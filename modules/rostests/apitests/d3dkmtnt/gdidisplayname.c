/*
 * PROJECT:     LiberNT D3DKMT API Tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Opening the adapter of a GDI display name
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 */

#include "precomp.h"
#include <wingdi.h>
#include <winuser.h>

typedef NTSTATUS (APIENTRY *PFN_KMT1)(PVOID);
typedef BOOL (WINAPI *PFN_ENUMDISPLAYDEVICESW)(LPCWSTR, DWORD, PDISPLAY_DEVICEW, DWORD);
typedef HDC (WINAPI *PFN_CREATEDCW)(LPCWSTR, LPCWSTR, LPCWSTR, const DEVMODEW *);
typedef BOOL (WINAPI *PFN_DELETEDC)(HDC);

static NTSTATUS
GdiNameGuarded(
    _In_ PFN_KMT1 Function,
    _In_opt_ PVOID Argument,
    _Out_ BOOLEAN *Raised)
{
    NTSTATUS Status = STATUS_UNSUCCESSFUL;

    *Raised = FALSE;
    _SEH2_TRY
    {
        Status = Function(Argument);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
        *Raised = TRUE;
    }
    _SEH2_END;
    return Status;
}

static VOID
GdiNameClose(
    _In_ D3DKMT_HANDLE hAdapter)
{
    PFN_KMT1 pClose = (PFN_KMT1)GetProcAddress(GetModuleHandleA("gdi32.dll"), "D3DKMTCloseAdapter");
    D3DKMT_CLOSEADAPTER Close;

    if (pClose == NULL || hAdapter == 0)
        return;
    Close.hAdapter = hAdapter;
    pClose(&Close);
}

static NTSTATUS
GdiNameOpen(
    _In_ PFN_KMT1 pOpenName,
    _In_opt_ PCWSTR Name,
    _Out_ D3DKMT_OPENADAPTERFROMGDIDISPLAYNAME *Open,
    _Out_ BOOLEAN *Raised)
{
    UINT Index;

    memset(Open, 0x5A, sizeof(*Open));
    if (Name != NULL)
    {
        ZeroMemory(Open->DeviceName, sizeof(Open->DeviceName));
        lstrcpynW(Open->DeviceName, Name, ARRAYSIZE(Open->DeviceName));
    }
    else
    {
        for (Index = 0; Index < ARRAYSIZE(Open->DeviceName); ++Index)
            Open->DeviceName[Index] = L'A';
    }
    return GdiNameGuarded(pOpenName, Open, Raised);
}

static VOID
GdiNameRefused(
    _In_ PFN_KMT1 pOpenName,
    _In_opt_ PCWSTR Name)
{
    D3DKMT_OPENADAPTERFROMGDIDISPLAYNAME Open;
    NTSTATUS Status;
    BOOLEAN Raised;

    Status = GdiNameOpen(pOpenName, Name, &Open, &Raised);
    ok(Status == STATUS_UNSUCCESSFUL && !Raised, "'%ls': status 0x%08lx%s\n", Name ? Name : L"(unterminated)", Status, Raised ? " raised" : "");
    ok(Open.hAdapter == 0x5A5A5A5A && Open.AdapterLuid.LowPart == 0x5A5A5A5A && Open.VidPnSourceId == 0x5A5A5A5A,
       "'%ls': outputs written\n", Name ? Name : L"(unterminated)");
    if (NT_SUCCESS(Status))
        GdiNameClose(Open.hAdapter);
}

static VOID
GdiNameOpens(
    _In_ PFN_KMT1 pOpenName,
    _In_ PCWSTR Name,
    _In_ LUID AdapterLuid,
    _In_ D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId)
{
    D3DKMT_OPENADAPTERFROMGDIDISPLAYNAME Open;
    NTSTATUS Status;
    BOOLEAN Raised;

    Status = GdiNameOpen(pOpenName, Name, &Open, &Raised);
    ok(Status == STATUS_SUCCESS && !Raised, "'%ls': status 0x%08lx%s\n", Name, Status, Raised ? " raised" : "");
    if (!NT_SUCCESS(Status) || Raised)
        return;
    ok(Open.hAdapter != 0, "'%ls': no adapter handle\n", Name);
    ok(Open.AdapterLuid.LowPart == AdapterLuid.LowPart && Open.AdapterLuid.HighPart == AdapterLuid.HighPart,
       "'%ls': LUID 0x%lx:0x%lx, the DC's adapter is 0x%lx:0x%lx\n", Name, Open.AdapterLuid.HighPart, Open.AdapterLuid.LowPart,
       AdapterLuid.HighPart, AdapterLuid.LowPart);
    ok(Open.VidPnSourceId == VidPnSourceId, "'%ls': source %u, the DC's source is %u\n", Name, Open.VidPnSourceId, VidPnSourceId);
    GdiNameClose(Open.hAdapter);
}

START_TEST(gdidisplayname)
{
    PFN_KMT1 pOpenName;
    PFN_KMT1 pOpenHdc;
    DISPLAY_DEVICEW Device;
    NTSTATUS Status;
    BOOLEAN Raised;
    PVOID NoAccess;
    PWCHAR Char;
    UINT Index;

    PFN_ENUMDISPLAYDEVICESW pEnumDisplayDevicesW = (PFN_ENUMDISPLAYDEVICESW)GetProcAddress(LoadLibraryA("user32.dll"), "EnumDisplayDevicesW");
    PFN_CREATEDCW pCreateDCW = (PFN_CREATEDCW)GetProcAddress(LoadLibraryA("gdi32.dll"), "CreateDCW");
    PFN_DELETEDC pDeleteDC = (PFN_DELETEDC)GetProcAddress(LoadLibraryA("gdi32.dll"), "DeleteDC");

    pOpenName = (PFN_KMT1)GetProcAddress(LoadLibraryA("gdi32.dll"), "D3DKMTOpenAdapterFromGdiDisplayName");
    pOpenHdc = (PFN_KMT1)GetProcAddress(GetModuleHandleA("gdi32.dll"), "D3DKMTOpenAdapterFromHdc");
    if (pOpenName == NULL || pOpenHdc == NULL || pEnumDisplayDevicesW == NULL || pCreateDCW == NULL || pDeleteDC == NULL)
    {
        skip("gdi32 does not export the adapter open entry points\n");
        return;
    }
    ok(GetProcAddress(LoadLibraryA("win32u.dll"), "NtGdiDdDDIOpenAdapterFromGdiDisplayName") == NULL,
       "win32u exports NtGdiDdDDIOpenAdapterFromGdiDisplayName\n");

    Status = GdiNameGuarded(pOpenName, NULL, &Raised);
    ok(Status == STATUS_UNSUCCESSFUL && !Raised, "no descriptor: status 0x%08lx%s\n", Status, Raised ? " raised" : "");
    NoAccess = VirtualAlloc(NULL, 0x1000, MEM_COMMIT | MEM_RESERVE, PAGE_NOACCESS);
    if (NoAccess != NULL)
    {
        Status = GdiNameGuarded(pOpenName, NoAccess, &Raised);
        ok(Status == STATUS_ACCESS_VIOLATION && Raised, "inaccessible descriptor: status 0x%08lx%s\n", Status, Raised ? " raised" : "");
    }

    for (Index = 0; Index < 4; ++Index)
    {
        D3DKMT_OPENADAPTERFROMHDC OpenHdc;
        HDC hDC;

        ZeroMemory(&Device, sizeof(Device));
        Device.cb = sizeof(Device);
        if (!pEnumDisplayDevicesW(NULL, Index, &Device, 0))
            break;
        if (!(Device.StateFlags & DISPLAY_DEVICE_ATTACHED_TO_DESKTOP))
            continue;
        hDC = pCreateDCW(NULL, Device.DeviceName, NULL, NULL);
        ok(hDC != NULL, "no DC for '%ls'\n", Device.DeviceName);
        if (hDC == NULL)
            continue;
        ZeroMemory(&OpenHdc, sizeof(OpenHdc));
        OpenHdc.hDc = hDC;
        Status = pOpenHdc(&OpenHdc);
        pDeleteDC(hDC);
        if (!NT_SUCCESS(Status))
        {
            skip("no adapter for the DC of '%ls' (0x%08lx)\n", Device.DeviceName, Status);
            continue;
        }
        GdiNameClose(OpenHdc.hAdapter);
        GdiNameOpens(pOpenName, Device.DeviceName, OpenHdc.AdapterLuid, OpenHdc.VidPnSourceId);
        if (Index == 0)
        {
            GdiNameOpens(pOpenName, L"DISPLAY", OpenHdc.AdapterLuid, OpenHdc.VidPnSourceId);
            for (Char = Device.DeviceName; *Char != L'\0'; ++Char)
            {
                if (*Char >= L'A' && *Char <= L'Z')
                    *Char += L'a' - L'A';
            }
            GdiNameOpens(pOpenName, Device.DeviceName, OpenHdc.AdapterLuid, OpenHdc.VidPnSourceId);
        }
    }
    GdiNameRefused(pOpenName, L"\\\\.\\DISPLAY999");
    GdiNameRefused(pOpenName, L"");
    GdiNameRefused(pOpenName, L"\\\\.\\DISPLAY1\\Monitor0");
    GdiNameRefused(pOpenName, NULL);
}
