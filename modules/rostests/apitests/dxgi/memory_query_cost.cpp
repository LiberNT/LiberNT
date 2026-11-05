/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     A video memory budget query costs about one kernel call, because graphics drivers make it per submit
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */
#undef DXGKDDI_INTERFACE_VERSION
#define DXGKDDI_INTERFACE_VERSION 0x11007
#include <apitest.h>
#include <windows.h>
#include <dxgi1_4.h>
#include <d3dkmthk.h>

#define QUERY_COUNT 2000
#define LOAD(Type, Name) reinterpret_cast<Type>(GetProcAddress(GetModuleHandleW(L"gdi32.dll"), Name))
#define MAXIMUM_KERNEL_CALLS 4.0

typedef HRESULT (WINAPI *PFN_CREATE_FACTORY)(REFIID, void **);

static const GUID IID_Factory1Local = {0x770aae78, 0xf26f, 0x4dba, {0xa8, 0x29, 0x25, 0x3c, 0x83, 0xd1, 0xb3, 0x87}};
static const GUID IID_Adapter3Local = {0x645967a4, 0x1392, 0x4310, {0xa7, 0x98, 0x80, 0x53, 0xce, 0x3e, 0x93, 0xfd}};

static double
Seconds(const LARGE_INTEGER *Start, const LARGE_INTEGER *End, const LARGE_INTEGER *Frequency)
{
    return (double)(End->QuadPart - Start->QuadPart) / (double)Frequency->QuadPart;
}

START_TEST(memory_query_cost)
{
    HMODULE Module = LoadLibraryW(L"dxgi.dll");
    PFN_CREATE_FACTORY CreateFactory;
    IDXGIFactory1 *Factory = NULL;
    IDXGIAdapter1 *Adapter = NULL;
    IDXGIAdapter3 *Adapter3 = NULL;
    DXGI_ADAPTER_DESC1 Desc;
    DXGI_QUERY_VIDEO_MEMORY_INFO Info;
    D3DKMT_OPENADAPTERFROMLUID Open = {};
    D3DKMT_QUERYVIDEOMEMORYINFO Query = {};
    D3DKMT_CLOSEADAPTER Close = {};
    LARGE_INTEGER Frequency, Start, End;
    double Kernel, Dxgi;
    ULONG Index;
    PFND3DKMT_OPENADAPTERFROMLUID OpenAdapter = LOAD(PFND3DKMT_OPENADAPTERFROMLUID, "D3DKMTOpenAdapterFromLuid");
    PFND3DKMT_QUERYVIDEOMEMORYINFO QueryMemory = LOAD(PFND3DKMT_QUERYVIDEOMEMORYINFO, "D3DKMTQueryVideoMemoryInfo");
    PFND3DKMT_CLOSEADAPTER CloseAdapter = LOAD(PFND3DKMT_CLOSEADAPTER, "D3DKMTCloseAdapter");

    if (!OpenAdapter || !QueryMemory || !CloseAdapter)
    {
        skip("gdi32 lacks the kernel adapter calls\n");
        return;
    }
    if (!Module)
    {
        skip("dxgi.dll is not available\n");
        return;
    }
    CreateFactory = (PFN_CREATE_FACTORY)GetProcAddress(Module, "CreateDXGIFactory1");
    ok(CreateFactory != NULL, "CreateDXGIFactory1 is not exported\n");
    if (!CreateFactory || FAILED(CreateFactory(IID_Factory1Local, (void **)&Factory)))
    {
        skip("No DXGI factory\n");
        return;
    }
    if (FAILED(Factory->EnumAdapters1(0, &Adapter)) ||
        FAILED(Adapter->QueryInterface(IID_Adapter3Local, (void **)&Adapter3)) ||
        FAILED(Adapter->GetDesc1(&Desc)))
    {
        skip("No IDXGIAdapter3\n");
        if (Adapter)
            Adapter->Release();
        Factory->Release();
        return;
    }

    Open.AdapterLuid = Desc.AdapterLuid;
    ok(OpenAdapter(&Open) >= 0, "D3DKMTOpenAdapterFromLuid failed\n");
    Query.hAdapter = Open.hAdapter;
    Query.MemorySegmentGroup = D3DKMT_MEMORY_SEGMENT_GROUP_LOCAL;

    QueryPerformanceFrequency(&Frequency);
    QueryPerformanceCounter(&Start);
    for (Index = 0; Index < QUERY_COUNT; ++Index)
        QueryMemory(&Query);
    QueryPerformanceCounter(&End);
    Kernel = Seconds(&Start, &End, &Frequency);

    ok_hex(Adapter3->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &Info), S_OK);
    QueryPerformanceCounter(&Start);
    for (Index = 0; Index < QUERY_COUNT; ++Index)
        Adapter3->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &Info);
    QueryPerformanceCounter(&End);
    Dxgi = Seconds(&Start, &End, &Frequency);

    trace("kernel query %.2f us, DXGI query %.2f us\n",
          Kernel * 1000000.0 / QUERY_COUNT, Dxgi * 1000000.0 / QUERY_COUNT);
    ok(Dxgi <= Kernel * MAXIMUM_KERNEL_CALLS, "A DXGI query costs %.1f kernel queries, expected at most %.0f\n",
       Dxgi / Kernel, MAXIMUM_KERNEL_CALLS);

    Close.hAdapter = Open.hAdapter;
    CloseAdapter(&Close);
    Adapter3->Release();
    Adapter->Release();
    Factory->Release();
}
