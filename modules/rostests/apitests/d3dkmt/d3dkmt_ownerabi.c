/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Video present source owner entry points reach the unversioned service with the v2 descriptor
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

typedef NTSTATUS (APIENTRY *PFN_OWNER_ANY)(const void *);

static NTSTATUS
CallGuarded(PFN_OWNER_ANY Function, const void *Argument, BOOL *Faulted)
{
    NTSTATUS Status = STATUS_UNSUCCESSFUL;

    *Faulted = FALSE;
    _SEH2_TRY
    {
        Status = Function(Argument);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
        *Faulted = TRUE;
    }
    _SEH2_END;
    return Status;
}

static void
CheckNull(PFN_OWNER_ANY Function, const char *Name, BOOL Converted)
{
    NTSTATUS Status;
    BOOL Faulted;

    Status = CallGuarded(Function, NULL, &Faulted);
    if (Converted)
        ok(Faulted && Status == STATUS_ACCESS_VIOLATION, "%s(NULL): 0x%08lx%s\n", Name, Status,
           Faulted ? " (faulted)" : "");
    else
        ok(!Faulted && Status == STATUS_INVALID_PARAMETER, "%s(NULL): 0x%08lx%s\n", Name, Status,
           Faulted ? " (faulted)" : "");
}

START_TEST(ownerabi)
{
    PFN_OWNER_ANY Owner0 = (PFN_OWNER_ANY)LoadD3DKMTProc("D3DKMTSetVidPnSourceOwner");
    PFN_OWNER_ANY Owner1 = (PFN_OWNER_ANY)LoadD3DKMTProc("D3DKMTSetVidPnSourceOwner1");
    PFN_OWNER_ANY Owner2 = (PFN_OWNER_ANY)LoadD3DKMTProc("D3DKMTSetVidPnSourceOwner2");
    PFN_OWNER_ANY Service = (PFN_OWNER_ANY)GetProcAddress(LoadLibraryW(L"win32u.dll"), "NtGdiDdDDISetVidPnSourceOwner");
    D3DKMT_VIDPNSOURCEOWNER_TYPE Type = D3DKMT_VIDPNSOURCEOWNER_EXCLUSIVE;
    D3DDDI_VIDEO_PRESENT_SOURCE_ID SourceId = 0;
    D3DKMT_SETVIDPNSOURCEOWNER2 Release;
    D3DKMT_HANDLE hAdapter, hDevice;
    NTSTATUS Status;
    BOOL Faulted;

    if (!Owner0 || !Owner1 || !Owner2 || !Service)
    {
        skip("owner entry points: v0 %p v1 %p v2 %p service %p\n", Owner0, Owner1, Owner2, Service);
        return;
    }
    CheckNull(Owner0, "D3DKMTSetVidPnSourceOwner", TRUE);
    CheckNull(Owner1, "D3DKMTSetVidPnSourceOwner1", TRUE);
    CheckNull(Owner2, "D3DKMTSetVidPnSourceOwner2", FALSE);
    CheckNull(Service, "NtGdiDdDDISetVidPnSourceOwner", FALSE);

    hAdapter = OpenAdapterFromDisplay1();
    hDevice = hAdapter ? CreateTestDevice(hAdapter) : 0;
    if (!hDevice)
    {
        skip("no device on \\\\.\\DISPLAY1\n");
        CloseAdapter(hAdapter);
        return;
    }

    /* A zero count releases every source the device owns, whatever the arrays and flags say. */
    memset(&Release, 0, sizeof(Release));
    Release.Version1.Version0.hDevice = hDevice;
    Release.Version1.Version0.pType = &Type;
    Release.Version1.Version0.pVidPnSourceId = &SourceId;
    Release.Version1.Flags.AllowOutputDuplication = 1;
    Status = CallGuarded(Service, &Release, &Faulted);
    ok(!Faulted && Status == STATUS_SUCCESS, "service release-all: 0x%08lx%s\n", Status, Faulted ? " (faulted)" : "");
    Status = CallGuarded(Owner2, &Release, &Faulted);
    ok(!Faulted && Status == STATUS_SUCCESS, "v2 release-all: 0x%08lx%s\n", Status, Faulted ? " (faulted)" : "");
    Status = CallGuarded(Owner1, &Release.Version1, &Faulted);
    ok(!Faulted && Status == STATUS_SUCCESS, "v1 release-all: 0x%08lx%s\n", Status, Faulted ? " (faulted)" : "");
    Status = CallGuarded(Owner0, &Release.Version1.Version0, &Faulted);
    ok(!Faulted && Status == STATUS_SUCCESS, "v0 release-all: 0x%08lx%s\n", Status, Faulted ? " (faulted)" : "");

    /* A claim without a device is refused. */
    Release.Version1.Version0.hDevice = 0;
    Release.Version1.Version0.VidPnSourceCount = 1;
    Status = CallGuarded(Service, &Release, &Faulted);
    ok(!Faulted && Status == STATUS_INVALID_PARAMETER, "service without a device: 0x%08lx%s\n", Status,
       Faulted ? " (faulted)" : "");

    DestroyTestDevice(hDevice);
    CloseAdapter(hAdapter);
}
