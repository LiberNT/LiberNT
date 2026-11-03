/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test the source region of window swap chains
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <apitest.h>
#include <initguid.h>
#include <d3d11.h>
#include <dxgi1_4.h>

#define WIDTH 128
#define HEIGHT 96
#define SOURCE_WIDTH 64
#define SOURCE_HEIGHT 48
#define INSIDE_PIXEL 0xffff0000
#define OUTSIDE_PIXEL 0xff0000ff

static const COLORREF Inside = RGB(255, 0, 0);
static const COLORREF Outside = RGB(0, 0, 255);

static BOOL
ScreenColorNear(HDC Screen, POINT Origin, LONG X, LONG Y, COLORREF Expected, COLORREF *Actual)
{
    COLORREF Pixel = GetPixel(Screen, Origin.x + X, Origin.y + Y);
    *Actual = Pixel;
    return Pixel != CLR_INVALID &&
           abs((int)GetRValue(Pixel) - (int)GetRValue(Expected)) <= 8 &&
           abs((int)GetGValue(Pixel) - (int)GetGValue(Expected)) <= 8 &&
           abs((int)GetBValue(Pixel) - (int)GetBValue(Expected)) <= 8;
}

static BOOL
WaitForScreenColor(HDC Screen, POINT Origin, LONG X, LONG Y, COLORREF Expected)
{
    DWORD Start = GetTickCount();
    COLORREF Actual;
    do
    {
        if (ScreenColorNear(Screen, Origin, X, Y, Expected, &Actual))
            return TRUE;
        Sleep(10);
    } while (GetTickCount() - Start < 2000);
    return FALSE;
}

static void
CheckSourceSize(IDXGISwapChain2 *Chain, UINT Width, UINT Height, HRESULT Expected, UINT ExpectedWidth,
                UINT ExpectedHeight)
{
    UINT ActualWidth = ~0u, ActualHeight = ~0u;
    HRESULT Hr = Chain->SetSourceSize(Width, Height);
    HRESULT Query = Chain->GetSourceSize(&ActualWidth, &ActualHeight);

    ok(Hr == Expected, "SetSourceSize(%u, %u) = %#lx, expected %#lx\n", Width, Height, Hr, Expected);
    ok(Query == S_OK && ActualWidth == ExpectedWidth && ActualHeight == ExpectedHeight,
       "After SetSourceSize(%u, %u): GetSourceSize %#lx %u x %u, expected %u x %u\n", Width, Height, Query,
       ActualWidth, ActualHeight, ExpectedWidth, ExpectedHeight);
}

static BOOL
PresentFrame(ID3D11DeviceContext *Context, IDXGISwapChain2 *Chain)
{
    static DWORD Pixels[WIDTH * HEIGHT];
    ID3D11Texture2D *Buffer = NULL;
    HRESULT Hr;

    for (UINT Y = 0; Y < HEIGHT; ++Y)
        for (UINT X = 0; X < WIDTH; ++X)
            Pixels[Y * WIDTH + X] = X < SOURCE_WIDTH && Y < SOURCE_HEIGHT ? INSIDE_PIXEL : OUTSIDE_PIXEL;
    Hr = Chain->GetBuffer(0, IID_ID3D11Texture2D, reinterpret_cast<void **>(&Buffer));
    ok(Hr == S_OK && Buffer, "GetBuffer: %#lx\n", Hr);
    if (!Buffer)
        return FALSE;
    Context->UpdateSubresource(Buffer, 0, NULL, Pixels, WIDTH * sizeof(DWORD), 0);
    Buffer->Release();
    Hr = Chain->Present(0, 0);
    ok(Hr == S_OK, "Present: %#lx\n", Hr);
    return Hr == S_OK;
}

START_TEST(hwnd_source)
{
    typedef HRESULT (WINAPI *CREATE_DEVICE)(IDXGIAdapter *, D3D_DRIVER_TYPE, HMODULE,
        UINT, const D3D_FEATURE_LEVEL *, UINT, UINT, ID3D11Device **,
        D3D_FEATURE_LEVEL *, ID3D11DeviceContext **);
    HMODULE Runtime = LoadLibraryW(L"d3d11.dll");
    if (!Runtime) { skip("D3D11 unavailable\n"); return; }
    CREATE_DEVICE Create = reinterpret_cast<CREATE_DEVICE>(GetProcAddress(Runtime, "D3D11CreateDevice"));
    if (!Create) { skip("D3D11CreateDevice unavailable\n"); return; }

    ID3D11Device *Device = NULL;
    ID3D11DeviceContext *Context = NULL;
    HRESULT Hr = Create(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
                        NULL, 0, D3D11_SDK_VERSION, &Device, NULL, &Context);
    if (FAILED(Hr)) { skip("Hardware D3D11 unavailable: %#lx\n", Hr); return; }

    IDXGIDevice *Dxgi = NULL;
    IDXGIAdapter *Adapter = NULL;
    IDXGIFactory2 *Factory = NULL;
    Hr = Device->QueryInterface(IID_IDXGIDevice, reinterpret_cast<void **>(&Dxgi));
    if (Dxgi) Hr = Dxgi->GetAdapter(&Adapter);
    if (Adapter) Hr = Adapter->GetParent(IID_IDXGIFactory2, reinterpret_cast<void **>(&Factory));
    ok(Hr == S_OK && Factory, "IDXGIFactory2: %#lx\n", Hr);

    POINT Origin = {300, 260};
    HWND Window = CreateWindowExW(WS_EX_TOPMOST | WS_EX_NOACTIVATE, L"STATIC", L"hwnd source",
                                  WS_POPUP | WS_VISIBLE, Origin.x, Origin.y, WIDTH, HEIGHT,
                                  NULL, NULL, NULL, NULL);
    ok(Window != NULL, "CreateWindow: %lu\n", GetLastError());
    HDC Screen = GetDC(NULL);
    IDXGISwapChain1 *Chain1 = NULL;
    IDXGISwapChain2 *Chain = NULL;
    if (Factory && Window && Screen)
    {
        DXGI_SWAP_CHAIN_DESC1 Desc = {};
        Desc.Width = WIDTH;
        Desc.Height = HEIGHT;
        Desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        Desc.SampleDesc.Count = 1;
        Desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        Desc.BufferCount = 2;
        Desc.Scaling = DXGI_SCALING_STRETCH;
        Desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
        Hr = Factory->CreateSwapChainForHwnd(Device, Window, &Desc, NULL, NULL, &Chain1);
        ok(Hr == S_OK && Chain1, "CreateSwapChainForHwnd: %#lx\n", Hr);
        if (Chain1) Hr = Chain1->QueryInterface(IID_IDXGISwapChain2, reinterpret_cast<void **>(&Chain));
        ok(Hr == S_OK && Chain, "IDXGISwapChain2: %#lx\n", Hr);
    }
    if (Chain)
    {
        CheckSourceSize(Chain, WIDTH + 1, HEIGHT, DXGI_ERROR_INVALID_CALL, WIDTH, HEIGHT);
        CheckSourceSize(Chain, 0, HEIGHT, S_OK, 0, HEIGHT);
        CheckSourceSize(Chain, WIDTH, 0, S_OK, WIDTH, 0);
        CheckSourceSize(Chain, 0, 0, S_OK, 0, 0);
        CheckSourceSize(Chain, SOURCE_WIDTH, SOURCE_HEIGHT, S_OK, SOURCE_WIDTH, SOURCE_HEIGHT);
        if (PresentFrame(Context, Chain))
        {
            BOOL Shown = WaitForScreenColor(Screen, Origin, WIDTH / 4, HEIGHT / 4, Inside);
            ok(Shown, "The frame never reached the screen\n");
            ok(WaitForScreenColor(Screen, Origin, WIDTH * 3 / 4, HEIGHT * 3 / 4, Inside),
               "The source region was not stretched over the window\n");
        }
        CheckSourceSize(Chain, WIDTH, HEIGHT, S_OK, WIDTH, HEIGHT);
        if (PresentFrame(Context, Chain))
        {
            ok(WaitForScreenColor(Screen, Origin, WIDTH * 3 / 4, HEIGHT * 3 / 4, Outside),
               "The full source never reached the screen\n");
        }
        CheckSourceSize(Chain, SOURCE_WIDTH, SOURCE_HEIGHT, S_OK, SOURCE_WIDTH, SOURCE_HEIGHT);
        Hr = Chain->ResizeBuffers(0, WIDTH, HEIGHT, DXGI_FORMAT_UNKNOWN, 0);
        ok(Hr == S_OK, "ResizeBuffers: %#lx\n", Hr);
        {
            UINT AfterWidth = ~0u, AfterHeight = ~0u;
            Hr = Chain->GetSourceSize(&AfterWidth, &AfterHeight);
            ok(Hr == S_OK && AfterWidth == WIDTH && AfterHeight == HEIGHT,
               "After ResizeBuffers: GetSourceSize %#lx %u x %u\n", Hr, AfterWidth, AfterHeight);
        }
    }

    if (Chain) Chain->Release();
    if (Chain1) Chain1->Release();
    Chain = NULL;
    Chain1 = NULL;
    if (Factory && Window && Screen)
    {
        DXGI_SWAP_CHAIN_DESC1 Desc = {};
        Desc.Width = WIDTH;
        Desc.Height = HEIGHT;
        Desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        Desc.SampleDesc.Count = 1;
        Desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        Desc.BufferCount = 2;
        Desc.Scaling = DXGI_SCALING_NONE;
        Desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
        Hr = Factory->CreateSwapChainForHwnd(Device, Window, &Desc, NULL, NULL, &Chain1);
        ok(Hr == S_OK && Chain1, "CreateSwapChainForHwnd(DXGI_SCALING_NONE): %#lx\n", Hr);
        if (Chain1) Chain1->QueryInterface(IID_IDXGISwapChain2, reinterpret_cast<void **>(&Chain));
        if (Chain)
        {
            CheckSourceSize(Chain, SOURCE_WIDTH, SOURCE_HEIGHT, S_OK, SOURCE_WIDTH, SOURCE_HEIGHT);
            if (PresentFrame(Context, Chain))
            {
                ok(WaitForScreenColor(Screen, Origin, WIDTH * 3 / 4, HEIGHT * 3 / 4, Inside),
                   "DXGI_SCALING_NONE: the source region was not stretched over the window\n");
                ok(WaitForScreenColor(Screen, Origin, WIDTH * 3 / 4, HEIGHT / 4, Inside),
                   "DXGI_SCALING_NONE: the source region does not cover the top right\n");
            }
        }
    }
    if (Chain) Chain->Release();
    if (Chain1) Chain1->Release();
    if (Screen) ReleaseDC(NULL, Screen);
    if (Window) DestroyWindow(Window);
    if (Factory) Factory->Release();
    if (Adapter) Adapter->Release();
    if (Dxgi) Dxgi->Release();
    Context->ClearState();
    Context->Release();
    Device->Release();
    FreeLibrary(Runtime);
}
