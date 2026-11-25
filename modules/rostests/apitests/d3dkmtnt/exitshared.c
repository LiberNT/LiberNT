/*
 * PROJECT:     LiberNT D3DKMT API Tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Process exit with a shared monitored fence NT handle still open
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 */

#include "precomp.h"

typedef NTSTATUS (APIENTRY *PFN_KMT1)(PVOID);
typedef NTSTATUS (APIENTRY *PFN_KMTSHARE)(UINT, const D3DKMT_HANDLE *, POBJECT_ATTRIBUTES, DWORD, HANDLE *);

static PFN_KMT1
ExitSharedWin32u(
    _In_ PCSTR Name)
{
    return (PFN_KMT1)GetProcAddress(LoadLibraryA("win32u.dll"), Name);
}

START_TEST(exitshared_child)
{
    PFN_KMTSHARE pShareObjects = (PFN_KMTSHARE)ExitSharedWin32u("NtGdiDdDDIShareObjects");
    D3DKMT_CREATESYNCHRONIZATIONOBJECT2 Fence;
    D3DKMT_ADAPTERINFO Adapters[16];
    D3DKMT_ENUMADAPTERS2 Enum;
    D3DKMT_CREATEDEVICE Device;
    OBJECT_ATTRIBUTES Attributes;
    HANDLE Shared = NULL;

    LoadLibraryA("gdi32.dll");
    ZeroMemory(&Enum, sizeof(Enum));
    Enum.NumAdapters = ARRAYSIZE(Adapters);
    Enum.pAdapters = Adapters;
    if (pShareObjects == NULL || !NT_SUCCESS(ExitSharedWin32u("NtGdiDdDDIEnumAdapters2")(&Enum)) || Enum.NumAdapters == 0)
        return;
    ZeroMemory(&Device, sizeof(Device));
    Device.hAdapter = Adapters[Enum.NumAdapters - 1].hAdapter;
    if (!NT_SUCCESS(ExitSharedWin32u("NtGdiDdDDICreateDevice")(&Device)))
        return;
    ZeroMemory(&Fence, sizeof(Fence));
    Fence.hDevice = Device.hDevice;
    Fence.Info.Type = D3DDDI_MONITORED_FENCE;
    Fence.Info.Flags.Shared = 1;
    Fence.Info.Flags.NtSecuritySharing = 1;
    if (!NT_SUCCESS(ExitSharedWin32u("NtGdiDdDDICreateSynchronizationObject")(&Fence)))
        return;
    InitializeObjectAttributes(&Attributes, NULL, 0, NULL, NULL);
    ok_hex(pShareObjects(1, &Fence.hSyncObject, &Attributes, GENERIC_ALL, &Shared), STATUS_SUCCESS);
}

START_TEST(exitshared)
{
    CHAR Path[MAX_PATH];
    CHAR CommandLine[MAX_PATH + 32];
    STARTUPINFOA Startup;
    PROCESS_INFORMATION Process;
    DWORD Wait;

    ok(GetModuleFileNameA(NULL, Path, ARRAYSIZE(Path)) != 0, "GetModuleFileName failed\n");
    lstrcpyA(CommandLine, "\"");
    lstrcatA(CommandLine, Path);
    lstrcatA(CommandLine, "\" exitshared_child");
    ZeroMemory(&Startup, sizeof(Startup));
    Startup.cb = sizeof(Startup);
    if (!CreateProcessA(Path, CommandLine, NULL, NULL, FALSE, 0, NULL, NULL, &Startup, &Process))
    {
        ok(FALSE, "CreateProcess failed %lu\n", GetLastError());
        return;
    }
    Wait = WaitForSingleObject(Process.hProcess, 5000);
    ok(Wait == WAIT_OBJECT_0, "child with an open shared fence handle did not exit: wait %lu\n", Wait);
    CloseHandle(Process.hThread);
    CloseHandle(Process.hProcess);
}
