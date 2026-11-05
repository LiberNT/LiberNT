/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     GPU signals of D3D11 fences while other devices come and go beside their pages
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arifing193@gmail.com>
 */

#include <apitest.h>
#include <initguid.h>
#include <d3d11_4.h>

#define FENCE_COUNT 6

typedef HRESULT (WINAPI *CREATE_DEVICE)(IDXGIAdapter *, D3D_DRIVER_TYPE, HMODULE, UINT, const D3D_FEATURE_LEVEL *,
                                         UINT, UINT, ID3D11Device **, D3D_FEATURE_LEVEL *, ID3D11DeviceContext **);

static ID3D11Device *CreateOtherDevice(CREATE_DEVICE Create)
{
    ID3D11Device *Device = NULL;
    ID3D11DeviceContext *Context = NULL;
    HRESULT Hr = Create(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0, NULL, 0, D3D11_SDK_VERSION, &Device, NULL, &Context);

    ok(Hr == S_OK, "second device: %#lx\n", Hr);
    if (Context)
    {
        Context->Flush();
        Context->Release();
    }
    return Device;
}

static void SignalAndWait(ID3D11DeviceContext4 *Context, ID3D11Fence *Fence, UINT Index, UINT64 Value)
{
    HANDLE Event = CreateEventW(NULL, FALSE, FALSE, NULL);
    HRESULT Hr = Context->Signal(Fence, Value);

    ok(Hr == S_OK, "fence %u Signal(%I64u): %#lx\n", Index, Value, Hr);
    Context->Flush();
    Hr = Fence->SetEventOnCompletion(Value, Event);
    ok(Hr == S_OK, "fence %u SetEventOnCompletion(%I64u): %#lx\n", Index, Value, Hr);
    ok(WaitForSingleObject(Event, 2000) == WAIT_OBJECT_0, "fence %u never reached %I64u\n", Index, Value);
    ok(Fence->GetCompletedValue() == Value, "fence %u completed value %I64u, expected %I64u\n", Index,
       Fence->GetCompletedValue(), Value);
    CloseHandle(Event);
}

static ID3D11Fence *CreateFence(ID3D11Device5 *Device, UINT Index)
{
    ID3D11Fence *Fence = NULL;
    HRESULT Hr = Device->CreateFence(0, D3D11_FENCE_FLAG_NONE, IID_ID3D11Fence, reinterpret_cast<void **>(&Fence));

    ok(Hr == S_OK && Fence, "fence %u CreateFence: %#lx\n", Index, Hr);
    return Fence;
}

static void TestNeighbors(CREATE_DEVICE Create, ID3D11Device5 *Device, ID3D11DeviceContext4 *Context)
{
    ID3D11Fence *Fences[FENCE_COUNT] = {};
    ID3D11Device *Other;
    UINT Index;

    for (Index = 0; Index < FENCE_COUNT - 1; ++Index)
        Fences[Index] = CreateFence(Device, Index);
    Other = CreateOtherDevice(Create);
    if (Fences[0]) Fences[0]->Release();
    Fences[0] = NULL;
    if (Other) Other->Release();
    /* The fence filling the freed page is flushed together with the other device's unmaps past the live fences. */
    Fences[FENCE_COUNT - 1] = CreateFence(Device, FENCE_COUNT - 1);
    for (Index = 1; Index < FENCE_COUNT; ++Index)
        if (Fences[Index]) SignalAndWait(Context, Fences[Index], Index, 1);
    ok(Device->GetDeviceRemovedReason() == S_OK, "device removed: %#lx\n", Device->GetDeviceRemovedReason());
    for (Index = 0; Index < FENCE_COUNT; ++Index)
        if (Fences[Index]) Fences[Index]->Release();
}

START_TEST(fence_neighbors)
{
    HMODULE Module = LoadLibraryW(L"d3d11.dll");
    CREATE_DEVICE Create = Module ? reinterpret_cast<CREATE_DEVICE>(GetProcAddress(Module, "D3D11CreateDevice")) : NULL;
    ID3D11Device *Device = NULL;
    ID3D11DeviceContext *Context = NULL;
    ID3D11Device5 *Device5 = NULL;
    ID3D11DeviceContext4 *Context4 = NULL;

    if (!Create || FAILED(Create(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0, NULL, 0, D3D11_SDK_VERSION, &Device, NULL, &Context)))
    {
        skip("no hardware D3D11 device\n");
        if (Module) FreeLibrary(Module);
        return;
    }
    Device->QueryInterface(IID_ID3D11Device5, reinterpret_cast<void **>(&Device5));
    Context->QueryInterface(IID_ID3D11DeviceContext4, reinterpret_cast<void **>(&Context4));
    if (Device5 && Context4)
    {
        ID3D11Fence *Probe = NULL;

        if (Device5->CreateFence(0, D3D11_FENCE_FLAG_NONE, IID_ID3D11Fence, reinterpret_cast<void **>(&Probe)) == E_NOTIMPL)
            skip("this device has no fences\n");
        else
        {
            if (Probe) Probe->Release();
            TestNeighbors(Create, Device5, Context4);
        }
    }
    else
        skip("no ID3D11Device5 / ID3D11DeviceContext4\n");
    if (Context4) Context4->Release();
    if (Device5) Device5->Release();
    Context->Release();
    Device->Release();
    FreeLibrary(Module);
}
