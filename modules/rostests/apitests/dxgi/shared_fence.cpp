/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Direct3D 11 fences shared through NT handles between devices
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arifing193@gmail.com>
 */

#include <apitest.h>
#include <initguid.h>
#include <d3d11_4.h>

typedef HRESULT (WINAPI *CREATE_DEVICE)(IDXGIAdapter *, D3D_DRIVER_TYPE, HMODULE, UINT, const D3D_FEATURE_LEVEL *,
                                         UINT, UINT, ID3D11Device **, D3D_FEATURE_LEVEL *, ID3D11DeviceContext **);

struct FenceDevice
{
    ID3D11Device5 *Device;
    ID3D11DeviceContext4 *Context;
};

static BOOL CreateFenceDevice(CREATE_DEVICE Create, FenceDevice *Out)
{
    ID3D11Device *Device = NULL;
    ID3D11DeviceContext *Context = NULL;
    HRESULT Hr;

    Out->Device = NULL;
    Out->Context = NULL;
    Hr = Create(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0, NULL, 0, D3D11_SDK_VERSION, &Device, NULL, &Context);
    if (FAILED(Hr))
        return FALSE;
    Device->QueryInterface(IID_ID3D11Device5, reinterpret_cast<void **>(&Out->Device));
    Context->QueryInterface(IID_ID3D11DeviceContext4, reinterpret_cast<void **>(&Out->Context));
    Device->Release();
    Context->Release();
    if (!Out->Device || !Out->Context)
    {
        if (Out->Device) Out->Device->Release();
        if (Out->Context) Out->Context->Release();
        return FALSE;
    }
    return TRUE;
}

static void ReleaseFenceDevice(FenceDevice *Device)
{
    if (Device->Context) Device->Context->Release();
    if (Device->Device) Device->Device->Release();
}

static BOOL ReachesValue(ID3D11Fence *Fence, UINT64 Value, DWORD Timeout)
{
    HANDLE Event = CreateEventW(NULL, FALSE, FALSE, NULL);
    HRESULT Hr = Fence->SetEventOnCompletion(Value, Event);
    BOOL Reached = SUCCEEDED(Hr) && WaitForSingleObject(Event, Timeout) == WAIT_OBJECT_0;

    ok(Hr == S_OK, "SetEventOnCompletion(%I64u): %#lx\n", Value, Hr);
    CloseHandle(Event);
    return Reached;
}

static void SignalNow(FenceDevice *Device, ID3D11Fence *Fence, UINT64 Value)
{
    HRESULT Hr = Device->Context->Signal(Fence, Value);

    ok(Hr == S_OK, "Signal(%I64u): %#lx\n", Value, Hr);
    Device->Context->Flush();
}

START_TEST(shared_fence)
{
    SECURITY_ATTRIBUTES Inherit = { sizeof(Inherit), NULL, TRUE };
    FenceDevice First, Second;
    ID3D11Fence *Fence = NULL, *Plain = NULL, *Opened = NULL, *Marker = NULL;
    HANDLE Handle = NULL, Inheritable = NULL, Bogus = (HANDLE)0xdeadbeef;
    DWORD HandleFlags = 0;
    HRESULT Hr;

    HMODULE Module = LoadLibraryW(L"d3d11.dll");
    if (!Module) { skip("d3d11.dll is unavailable\n"); return; }
    CREATE_DEVICE Create = reinterpret_cast<CREATE_DEVICE>(GetProcAddress(Module, "D3D11CreateDevice"));
    if (!Create || !CreateFenceDevice(Create, &First))
    {
        skip("no D3D11 device with fences\n");
        FreeLibrary(Module);
        return;
    }
    if (!CreateFenceDevice(Create, &Second))
    {
        skip("no second D3D11 device\n");
        ReleaseFenceDevice(&First);
        FreeLibrary(Module);
        return;
    }

    Hr = First.Device->CreateFence(0, static_cast<D3D11_FENCE_FLAG>(0), IID_ID3D11Fence, reinterpret_cast<void **>(&Plain));
    ok(Hr == S_OK, "CreateFence(none): %#lx\n", Hr);
    if (Plain)
    {
        Hr = Plain->CreateSharedHandle(NULL, GENERIC_ALL, NULL, &Bogus);
        ok(Hr == E_INVALIDARG, "CreateSharedHandle(unshared fence): %#lx\n", Hr);
        ok(Bogus == (HANDLE)0xdeadbeef, "unshared fence wrote handle %p\n", Bogus);
        Plain->Release();
    }

    Hr = First.Device->CreateFence(3, D3D11_FENCE_FLAG_SHARED, IID_ID3D11Fence, reinterpret_cast<void **>(&Fence));
    ok(Hr == S_OK, "CreateFence(SHARED): %#lx\n", Hr);
    if (Fence)
    {
        Hr = Fence->CreateSharedHandle(NULL, GENERIC_ALL, NULL, &Handle);
        ok(Hr == S_OK, "CreateSharedHandle: %#lx\n", Hr);
        Hr = Fence->CreateSharedHandle(&Inherit, GENERIC_ALL, NULL, &Inheritable);
        ok(Hr == S_OK, "CreateSharedHandle(inheritable): %#lx\n", Hr);
        if (Inheritable)
        {
            ok(GetHandleInformation(Inheritable, &HandleFlags), "GetHandleInformation failed: %lu\n", GetLastError());
            ok(HandleFlags & HANDLE_FLAG_INHERIT, "shared handle flags %#lx, expected inheritable\n", HandleFlags);
            CloseHandle(Inheritable);
        }
    }
    if (Handle)
    {
        Hr = Second.Device->OpenSharedFence(Handle, IID_ID3D11Fence, reinterpret_cast<void **>(&Opened));
        ok(Hr == S_OK, "OpenSharedFence on another device: %#lx\n", Hr);
        Hr = Second.Device->OpenSharedFence(NULL, IID_ID3D11Fence, reinterpret_cast<void **>(&Marker));
        ok(Hr == E_INVALIDARG, "OpenSharedFence(NULL): %#lx\n", Hr);
        if (Marker) { Marker->Release(); Marker = NULL; }
    }
    if (Opened)
    {
        ok(Opened->GetCompletedValue() == 3, "opened fence reads %I64u, created at 3\n", Opened->GetCompletedValue());
        SignalNow(&First, Fence, 5);
        ok(ReachesValue(Opened, 5, 5000), "first device's signal did not reach the opened fence\n");
        SignalNow(&Second, Opened, 6);
        ok(ReachesValue(Fence, 6, 5000), "second device's signal did not reach the creating fence\n");

        Hr = Second.Device->CreateFence(0, static_cast<D3D11_FENCE_FLAG>(0), IID_ID3D11Fence, reinterpret_cast<void **>(&Marker));
        ok(Hr == S_OK, "CreateFence(marker): %#lx\n", Hr);
        if (Marker)
        {
            Hr = Second.Context->Wait(Opened, 8);
            ok(Hr == S_OK, "second device Wait(8): %#lx\n", Hr);
            SignalNow(&Second, Marker, 1);
            Sleep(300);
            ok(Marker->GetCompletedValue() == 0, "second device passed an unsatisfied wait\n");
            SignalNow(&First, Fence, 8);
            ok(ReachesValue(Marker, 1, 5000), "first device's signal did not release the second device's wait\n");
            Marker->Release();
        }
        Opened->Release();
    }
    if (Handle) CloseHandle(Handle);
    if (Fence) Fence->Release();
    ReleaseFenceDevice(&Second);
    ReleaseFenceDevice(&First);
    FreeLibrary(Module);
}
