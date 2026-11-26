/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     UpdateSubresource writes the buffers of a window swap chain
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <apitest.h>
#include <initguid.h>
#include <d3d11.h>
#include <dxgi1_2.h>

#define WIDTH 64
#define HEIGHT 32
#define FULL_PIXEL 0xff2040c0u
#define BOX_PIXEL 0xffc04020u

typedef HRESULT (WINAPI *CREATE_DEVICE)(IDXGIAdapter *, D3D_DRIVER_TYPE, HMODULE, UINT, const D3D_FEATURE_LEVEL *,
        UINT, UINT, ID3D11Device **, D3D_FEATURE_LEVEL *, ID3D11DeviceContext **);

static void CheckContents(ID3D11Device *Device, ID3D11DeviceContext *Context, ID3D11Texture2D *Buffer,
        DXGI_SWAP_EFFECT Effect)
{
    D3D11_TEXTURE2D_DESC Desc = {};
    D3D11_MAPPED_SUBRESOURCE Map = {};
    ID3D11Texture2D *Staging = NULL;
    UINT Wrong = 0, FirstX = 0, FirstY = 0;
    DWORD First = 0;
    HRESULT Hr;

    Buffer->GetDesc(&Desc);
    Desc.Usage = D3D11_USAGE_STAGING;
    Desc.BindFlags = 0;
    Desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    Desc.MiscFlags = 0;
    Hr = Device->CreateTexture2D(&Desc, NULL, &Staging);
    ok(Hr == S_OK, "swap effect %u: staging texture %#lx\n", Effect, Hr);
    if (FAILED(Hr))
        return;
    Context->CopyResource(Staging, Buffer);
    Hr = Context->Map(Staging, 0, D3D11_MAP_READ, 0, &Map);
    ok(Hr == S_OK, "swap effect %u: Map %#lx\n", Effect, Hr);
    if (SUCCEEDED(Hr))
    {
        for (UINT Y = 0; Y < HEIGHT; ++Y)
        {
            const DWORD *Row = reinterpret_cast<const DWORD *>(static_cast<const BYTE *>(Map.pData) + Y * Map.RowPitch);
            for (UINT X = 0; X < WIDTH; ++X)
            {
                DWORD Expected = X >= 8 && X < 24 && Y >= 4 && Y < 12 ? BOX_PIXEL : FULL_PIXEL;
                if (Row[X] != Expected && !Wrong++)
                {
                    First = Row[X];
                    FirstX = X;
                    FirstY = Y;
                }
            }
        }
        ok(!Wrong, "swap effect %u: %u wrong pixels, first (%u,%u) %08lx\n", Effect, Wrong, FirstX, FirstY, First);
        Context->Unmap(Staging, 0);
    }
    Staging->Release();
}

static void CheckEffect(ID3D11Device *Device, ID3D11DeviceContext *Context, IDXGIFactory2 *Factory, HWND Window,
        DXGI_SWAP_EFFECT Effect)
{
    static DWORD Full[WIDTH * HEIGHT], Box[16 * 8];
    DXGI_SWAP_CHAIN_DESC1 Desc = {};
    D3D11_BOX Region = {8, 4, 0, 24, 12, 1};
    IDXGISwapChain1 *Chain = NULL;
    ID3D11Texture2D *Buffer = NULL;
    HRESULT Hr;

    for (UINT i = 0; i < ARRAYSIZE(Full); ++i)
        Full[i] = FULL_PIXEL;
    for (UINT i = 0; i < ARRAYSIZE(Box); ++i)
        Box[i] = BOX_PIXEL;
    Desc.Width = WIDTH;
    Desc.Height = HEIGHT;
    Desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    Desc.SampleDesc.Count = 1;
    Desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    Desc.BufferCount = 2;
    Desc.Scaling = DXGI_SCALING_STRETCH;
    Desc.SwapEffect = Effect;
    Hr = Factory->CreateSwapChainForHwnd(Device, Window, &Desc, NULL, NULL, &Chain);
    ok(Hr == S_OK, "swap effect %u: CreateSwapChainForHwnd %#lx\n", Effect, Hr);
    if (FAILED(Hr))
        return;
    Hr = Chain->GetBuffer(0, IID_ID3D11Texture2D, reinterpret_cast<void **>(&Buffer));
    ok(Hr == S_OK, "swap effect %u: GetBuffer %#lx\n", Effect, Hr);
    if (SUCCEEDED(Hr))
    {
        Context->UpdateSubresource(Buffer, 0, NULL, Full, WIDTH * sizeof(DWORD), 0);
        Context->UpdateSubresource(Buffer, 0, &Region, Box, 16 * sizeof(DWORD), 0);
        CheckContents(Device, Context, Buffer, Effect);
        Buffer->Release();
    }
    Chain->Release();
}

START_TEST(swapchain_upload)
{
    HMODULE Runtime = LoadLibraryW(L"d3d11.dll");
    CREATE_DEVICE Create = Runtime ? reinterpret_cast<CREATE_DEVICE>(GetProcAddress(Runtime, "D3D11CreateDevice")) : NULL;
    ID3D11Device *Device = NULL;
    ID3D11DeviceContext *Context = NULL;
    IDXGIDevice *Dxgi = NULL;
    IDXGIAdapter *Adapter = NULL;
    IDXGIFactory2 *Factory = NULL;
    HWND Window;
    HRESULT Hr;

    if (!Create)
    {
        skip("D3D11CreateDevice unavailable\n");
        return;
    }
    Hr = Create(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, D3D11_CREATE_DEVICE_BGRA_SUPPORT, NULL, 0, D3D11_SDK_VERSION,
            &Device, NULL, &Context);
    if (FAILED(Hr))
    {
        skip("Hardware D3D11 unavailable: %#lx\n", Hr);
        return;
    }
    Hr = Device->QueryInterface(IID_IDXGIDevice, reinterpret_cast<void **>(&Dxgi));
    if (Dxgi)
        Hr = Dxgi->GetAdapter(&Adapter);
    if (Adapter)
        Hr = Adapter->GetParent(IID_IDXGIFactory2, reinterpret_cast<void **>(&Factory));
    ok(Hr == S_OK && Factory, "IDXGIFactory2: %#lx\n", Hr);
    Window = CreateWindowExW(WS_EX_TOOLWINDOW, L"STATIC", L"swapchain upload", WS_POPUP, 0, 0, WIDTH, HEIGHT,
            NULL, NULL, NULL, NULL);
    ok(Window != NULL, "CreateWindow: %lu\n", GetLastError());
    if (Factory && Window)
    {
        CheckEffect(Device, Context, Factory, Window, DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL);
        CheckEffect(Device, Context, Factory, Window, DXGI_SWAP_EFFECT_DISCARD);
    }
    if (Window)
        DestroyWindow(Window);
    if (Factory)
        Factory->Release();
    if (Adapter)
        Adapter->Release();
    if (Dxgi)
        Dxgi->Release();
    Context->Release();
    Device->Release();
}
