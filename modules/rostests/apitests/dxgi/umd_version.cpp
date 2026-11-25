/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     IDXGIAdapter::CheckInterfaceSupport reports the adapter's user-mode driver version
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */
#undef DXGKDDI_INTERFACE_VERSION
#define DXGKDDI_INTERFACE_VERSION 0x11007
#include <apitest.h>
#include <windows.h>
#include <initguid.h>
#include <dxgi.h>
#include <d3d10.h>
#include <d3d11.h>
#include <d3dkmthk.h>

#define LOAD(Type, Name) reinterpret_cast<Type>(GetProcAddress(GetModuleHandleW(L"gdi32.dll"), Name))
#define UMD_SENTINEL 0x1111111111111111LL
#define HI(v) (ULONG)((ULONGLONG)(v) >> 32)
#define LO(v) (ULONG)(v)

typedef HRESULT (WINAPI *PFN_CREATE_FACTORY)(REFIID, void **);

static BOOL
KernelUmdVersion(LUID Luid, LARGE_INTEGER *Version)
{
    PFND3DKMT_OPENADAPTERFROMLUID OpenAdapter = LOAD(PFND3DKMT_OPENADAPTERFROMLUID, "D3DKMTOpenAdapterFromLuid");
    PFND3DKMT_QUERYADAPTERINFO QueryInfo = LOAD(PFND3DKMT_QUERYADAPTERINFO, "D3DKMTQueryAdapterInfo");
    PFND3DKMT_CLOSEADAPTER CloseAdapter = LOAD(PFND3DKMT_CLOSEADAPTER, "D3DKMTCloseAdapter");
    D3DKMT_OPENADAPTERFROMLUID Open = {};
    D3DKMT_QUERYADAPTERINFO Query = {};
    D3DKMT_UMD_DRIVER_VERSION Umd = {};
    D3DKMT_CLOSEADAPTER Close = {};
    NTSTATUS Status;

    if (!OpenAdapter || !QueryInfo || !CloseAdapter)
        return FALSE;
    Open.AdapterLuid = Luid;
    if (OpenAdapter(&Open) < 0)
        return FALSE;
    Query.hAdapter = Open.hAdapter;
    Query.Type = KMTQAITYPE_UMD_DRIVER_VERSION;
    Query.pPrivateDriverData = &Umd;
    Query.PrivateDriverDataSize = sizeof(Umd);
    Status = QueryInfo(&Query);
    Close.hAdapter = Open.hAdapter;
    CloseAdapter(&Close);
    if (Status < 0)
        return FALSE;
    *Version = Umd.DriverVersion;
    return TRUE;
}

static void
CheckAdapter(IDXGIAdapter1 *Adapter, UINT Index)
{
    DXGI_ADAPTER_DESC1 Desc;
    LARGE_INTEGER Device, D3D10, D3D11, Kernel;
    HRESULT hrDevice, hrD3D10, hrD3D11, hrNull;

    ok(SUCCEEDED(Adapter->GetDesc1(&Desc)), "adapter %u: GetDesc1 failed\n", Index);
    Device.QuadPart = D3D10.QuadPart = D3D11.QuadPart = UMD_SENTINEL;
    hrDevice = Adapter->CheckInterfaceSupport(__uuidof(IDXGIDevice), &Device);
    hrD3D10 = Adapter->CheckInterfaceSupport(__uuidof(ID3D10Device), &D3D10);
    hrD3D11 = Adapter->CheckInterfaceSupport(__uuidof(ID3D11Device), &D3D11);
    hrNull = Adapter->CheckInterfaceSupport(__uuidof(IDXGIDevice), NULL);

    ok(hrD3D11 == DXGI_ERROR_UNSUPPORTED, "adapter %u: ID3D11Device returned %#lx\n", Index, hrD3D11);
    ok(D3D11.QuadPart == UMD_SENTINEL, "adapter %u: ID3D11Device wrote 0x%08lx%08lx\n", Index, HI(D3D11.QuadPart), LO(D3D11.QuadPart));
    ok(hrD3D10 == hrDevice, "adapter %u: ID3D10Device %#lx, IDXGIDevice %#lx\n", Index, hrD3D10, hrDevice);
    ok(hrNull == hrDevice, "adapter %u: no version pointer %#lx, IDXGIDevice %#lx\n", Index, hrNull, hrDevice);
    if (FAILED(hrDevice))
    {
        ok(hrDevice == DXGI_ERROR_UNSUPPORTED, "adapter %u: IDXGIDevice returned %#lx\n", Index, hrDevice);
        ok(Device.QuadPart == UMD_SENTINEL, "adapter %u: unsupported IDXGIDevice wrote 0x%08lx%08lx\n", Index,
           HI(Device.QuadPart), LO(Device.QuadPart));
        ok(D3D10.QuadPart == UMD_SENTINEL, "adapter %u: unsupported ID3D10Device wrote 0x%08lx%08lx\n", Index,
           HI(D3D10.QuadPart), LO(D3D10.QuadPart));
        return;
    }
    ok(hrDevice == S_OK, "adapter %u: IDXGIDevice returned %#lx\n", Index, hrDevice);
    ok(D3D10.QuadPart == Device.QuadPart, "adapter %u: ID3D10Device version 0x%08lx%08lx, IDXGIDevice 0x%08lx%08lx\n",
       Index, HI(D3D10.QuadPart), LO(D3D10.QuadPart), HI(Device.QuadPart), LO(Device.QuadPart));
    if (!KernelUmdVersion(Desc.AdapterLuid, &Kernel))
    {
        skip("adapter %u: no kernel user-mode driver version\n", Index);
        return;
    }
    ok(Device.QuadPart == Kernel.QuadPart, "adapter %u '%ls': UMD version 0x%08lx%08lx, kernel 0x%08lx%08lx\n",
       Index, Desc.Description, HI(Device.QuadPart), LO(Device.QuadPart), HI(Kernel.QuadPart), LO(Kernel.QuadPart));
}

START_TEST(umd_version)
{
    HMODULE Module = LoadLibraryW(L"dxgi.dll");
    PFN_CREATE_FACTORY CreateFactory;
    IDXGIFactory1 *Factory = NULL;
    IDXGIAdapter1 *Adapter = NULL;
    UINT Index;

    LoadLibraryW(L"gdi32.dll");
    CreateFactory = Module ? reinterpret_cast<PFN_CREATE_FACTORY>(GetProcAddress(Module, "CreateDXGIFactory1")) : NULL;
    if (!CreateFactory || FAILED(CreateFactory(__uuidof(IDXGIFactory1), reinterpret_cast<void **>(&Factory))))
    {
        skip("no DXGI factory\n");
        return;
    }
    for (Index = 0; SUCCEEDED(Factory->EnumAdapters1(Index, &Adapter)); ++Index)
    {
        CheckAdapter(Adapter, Index);
        Adapter->Release();
    }
    ok(Index != 0, "no adapter\n");
    Factory->Release();
}
