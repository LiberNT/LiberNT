/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     IDXGIDevice GPU thread priority of a Direct3D 11 device
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arifing193@gmail.com>
 */

#include <apitest.h>
#include <initguid.h>
#include <d3d11.h>
#include <dxgi.h>

typedef HRESULT (WINAPI *CREATE_DEVICE)(IDXGIAdapter *, D3D_DRIVER_TYPE, HMODULE, UINT, const D3D_FEATURE_LEVEL *,
                                         UINT, UINT, ID3D11Device **, D3D_FEATURE_LEVEL *, ID3D11DeviceContext **);

START_TEST(gpu_priority)
{
    static const struct { INT Value; HRESULT Set; INT Get; } Values[] =
    {
        { 0, S_OK, 0 }, { 7, S_OK, 7 }, { -7, S_OK, -7 }, { 8, E_INVALIDARG, -7 }, { -8, E_INVALIDARG, -7 }, { 3, S_OK, 3 },
    };
    ID3D11Device *Device = NULL;
    IDXGIDevice *Dxgi = NULL;
    INT Priority;
    HRESULT Hr;
    UINT Index;

    HMODULE Module = LoadLibraryW(L"d3d11.dll");
    if (!Module) { skip("d3d11.dll is unavailable\n"); return; }
    CREATE_DEVICE Create = reinterpret_cast<CREATE_DEVICE>(GetProcAddress(Module, "D3D11CreateDevice"));
    if (!Create || FAILED(Create(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0, NULL, 0, D3D11_SDK_VERSION, &Device, NULL, NULL)))
    {
        skip("no D3D11 device\n");
        FreeLibrary(Module);
        return;
    }
    Hr = Device->QueryInterface(IID_IDXGIDevice, reinterpret_cast<void **>(&Dxgi));
    ok(Hr == S_OK, "IDXGIDevice: %#lx\n", Hr);
    if (Dxgi)
    {
        Priority = 0x55;
        Hr = Dxgi->GetGPUThreadPriority(&Priority);
        ok(Hr == S_OK && Priority == 0, "initial GetGPUThreadPriority: %#lx, %d\n", Hr, Priority);
        Hr = Dxgi->GetGPUThreadPriority(NULL);
        ok(Hr == E_INVALIDARG, "GetGPUThreadPriority(NULL): %#lx\n", Hr);
        for (Index = 0; Index < ARRAYSIZE(Values); ++Index)
        {
            Hr = Dxgi->SetGPUThreadPriority(Values[Index].Value);
            ok(Hr == Values[Index].Set, "SetGPUThreadPriority(%d): %#lx\n", Values[Index].Value, Hr);
            Priority = 0x55;
            Hr = Dxgi->GetGPUThreadPriority(&Priority);
            ok(Hr == S_OK && Priority == Values[Index].Get, "GetGPUThreadPriority after %d: %#lx, %d\n",
               Values[Index].Value, Hr, Priority);
        }
        Dxgi->Release();
    }
    Device->Release();
    FreeLibrary(Module);
}
