/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     ID3D11Device counter queries
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif193@gmail.com>
 */

#include <apitest.h>
#include <initguid.h>
#include <d3d11.h>

START_TEST(counters)
{
    typedef HRESULT (WINAPI *CREATE_DEVICE)(IDXGIAdapter *, D3D_DRIVER_TYPE, HMODULE,
        UINT, const D3D_FEATURE_LEVEL *, UINT, UINT, ID3D11Device **,
        D3D_FEATURE_LEVEL *, ID3D11DeviceContext **);
    HMODULE Module = LoadLibraryW(L"d3d11.dll");
    if (!Module) { skip("d3d11.dll is unavailable\n"); return; }
    CREATE_DEVICE Create = reinterpret_cast<CREATE_DEVICE>(GetProcAddress(Module, "D3D11CreateDevice"));
    if (!Create) { skip("D3D11CreateDevice is unavailable\n"); FreeLibrary(Module); return; }
    ID3D11Device *Device = NULL;
    HRESULT Hr = Create(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0, NULL, 0, D3D11_SDK_VERSION, &Device, NULL, NULL);
    if (FAILED(Hr)) { skip("Hardware D3D11 unavailable: %#lx\n", Hr); FreeLibrary(Module); return; }

    D3D11_COUNTER_INFO Info = {};
    Device->CheckCounterInfo(&Info);
    trace("LastDeviceDependentCounter %#x simultaneous %u units %u\n", Info.LastDeviceDependentCounter,
          Info.NumSimultaneousCounters, (UINT)Info.NumDetectableParallelUnits);
    ok(Info.LastDeviceDependentCounter == 0 || Info.LastDeviceDependentCounter >= D3D11_COUNTER_DEVICE_DEPENDENT_0,
       "LastDeviceDependentCounter %#x\n", Info.LastDeviceDependentCounter);

    D3D11_COUNTER_DESC Desc = {D3D11_COUNTER_DEVICE_DEPENDENT_0, 0};
    D3D11_COUNTER_TYPE Type = D3D11_COUNTER_TYPE_FLOAT32;
    UINT Active = 0;
    Hr = Device->CheckCounter(&Desc, NULL, &Active, NULL, NULL, NULL, NULL, NULL, NULL);
    ok(Hr == E_INVALIDARG, "CheckCounter without a type pointer: %#lx\n", Hr);
    Desc.MiscFlags = 1;
    Hr = Device->CheckCounter(&Desc, &Type, &Active, NULL, NULL, NULL, NULL, NULL, NULL);
    ok(Hr == E_INVALIDARG, "CheckCounter with MiscFlags: %#lx\n", Hr);
    Desc.MiscFlags = 0;
    Desc.Counter = static_cast<D3D11_COUNTER>(0);
    Hr = Device->CheckCounter(&Desc, &Type, &Active, NULL, NULL, NULL, NULL, NULL, NULL);
    ok(Hr == E_INVALIDARG, "CheckCounter below the device-dependent range: %#lx\n", Hr);

    ID3D11Counter *Counter = NULL;
    Hr = Device->CreateCounter(&Desc, &Counter);
    ok(Hr == E_INVALIDARG, "CreateCounter below the device-dependent range: %#lx\n", Hr);
    if (SUCCEEDED(Hr) && Counter) Counter->Release();

    Desc.Counter = static_cast<D3D11_COUNTER>(Info.LastDeviceDependentCounter >= D3D11_COUNTER_DEVICE_DEPENDENT_0 ?
        Info.LastDeviceDependentCounter + 1 : D3D11_COUNTER_DEVICE_DEPENDENT_0);
    Hr = Device->CheckCounter(&Desc, &Type, &Active, NULL, NULL, NULL, NULL, NULL, NULL);
    ok(Hr == E_INVALIDARG, "CheckCounter past the last device-dependent counter: %#lx\n", Hr);
    Counter = NULL;
    Hr = Device->CreateCounter(&Desc, &Counter);
    ok(Hr == E_INVALIDARG, "CreateCounter past the last device-dependent counter: %#lx\n", Hr);
    if (SUCCEEDED(Hr) && Counter) Counter->Release();

    if (Info.LastDeviceDependentCounter >= D3D11_COUNTER_DEVICE_DEPENDENT_0)
    {
        Desc.Counter = D3D11_COUNTER_DEVICE_DEPENDENT_0;
        Hr = Device->CreateCounter(&Desc, NULL);
        ok(Hr == S_FALSE, "CreateCounter validation-only call: %#lx\n", Hr);
    }

    UINT Levels = 0x55;
    Hr = Device->CheckMultisampleQualityLevels(DXGI_FORMAT_R8G8B8A8_UNORM, 1, &Levels);
    ok(Hr == S_OK && Levels == 1, "One-sample quality levels: %#lx %u\n", Hr, Levels);
    Levels = 0x55;
    Hr = Device->CheckMultisampleQualityLevels(DXGI_FORMAT_R8G8B8A8_UNORM, 0, &Levels);
    ok(Hr == E_FAIL && Levels == 0, "Zero-sample quality levels: %#lx %u\n", Hr, Levels);
    Levels = 0x55;
    Hr = Device->CheckMultisampleQualityLevels(DXGI_FORMAT_R8G8B8A8_UNORM, 33, &Levels);
    ok(Hr == E_FAIL && Levels == 0, "33-sample quality levels: %#lx %u\n", Hr, Levels);
    Hr = Device->CheckMultisampleQualityLevels(DXGI_FORMAT_R8G8B8A8_UNORM, 4, NULL);
    ok(Hr == E_INVALIDARG, "Quality levels without an output: %#lx\n", Hr);

    Device->Release();
    FreeLibrary(Module);
}
