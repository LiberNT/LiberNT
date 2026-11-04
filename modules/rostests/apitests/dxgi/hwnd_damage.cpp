/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test that partial presents of window swap chains keep undamaged pixels on screen
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arifing193@gmail.com>
 */

#include <apitest.h>
#include <initguid.h>
#include <d3d11.h>
#include <dxgi1_2.h>

#define WIDTH 128
#define HEIGHT 96

struct DAMAGE_FRAME
{
    RECT Rect;
    DWORD Pixel;
    COLORREF Screen;
};

static const DAMAGE_FRAME Frames[] =
{
    {{0, 0, WIDTH, HEIGHT}, 0xffff0000, RGB(255, 0, 0)},
    {{16, 12, 48, 40}, 0xff00ff00, RGB(0, 255, 0)},
    {{72, 48, 112, 84}, 0xff0000ff, RGB(0, 0, 255)},
    {{80, 8, 120, 28}, 0xffffffff, RGB(255, 255, 255)},
    {{24, 56, 56, 88}, 0xffffff00, RGB(255, 255, 0)},
};

static const POINT Background[] = {{4, 4}, {64, 44}, {120, 90}, {4, 90}};

typedef HRESULT (WINAPI *CREATE_DEVICE)(IDXGIAdapter *, D3D_DRIVER_TYPE, HMODULE, UINT, const D3D_FEATURE_LEVEL *,
                                         UINT, UINT, ID3D11Device **, D3D_FEATURE_LEVEL *, ID3D11DeviceContext **);

static BOOL ScreenColorNear(HDC Screen, POINT Origin, LONG X, LONG Y, COLORREF Expected, COLORREF *Actual)
{
    COLORREF Pixel = GetPixel(Screen, Origin.x + X, Origin.y + Y);

    *Actual = Pixel;
    return Pixel != CLR_INVALID &&
           abs((int)GetRValue(Pixel) - (int)GetRValue(Expected)) <= 8 &&
           abs((int)GetGValue(Pixel) - (int)GetGValue(Expected)) <= 8 &&
           abs((int)GetBValue(Pixel) - (int)GetBValue(Expected)) <= 8;
}

static void Pump(DWORD Milliseconds)
{
    DWORD Start = GetTickCount();
    MSG Message;

    while (GetTickCount() - Start < Milliseconds)
    {
        while (PeekMessageW(&Message, NULL, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&Message);
            DispatchMessageW(&Message);
        }
        Sleep(10);
    }
}

static BOOL WaitForScreenColor(HDC Screen, POINT Origin, LONG X, LONG Y, COLORREF Expected)
{
    DWORD Start = GetTickCount();
    COLORREF Actual;

    do
    {
        if (ScreenColorNear(Screen, Origin, X, Y, Expected, &Actual))
            return TRUE;
        Pump(10);
    } while (GetTickCount() - Start < 2000);
    return FALSE;
}

static void CheckFrame(HDC Screen, POINT Origin, UINT Last)
{
    COLORREF Actual;

    for (UINT i = 1; i <= Last; ++i)
    {
        const RECT &Rect = Frames[i].Rect;
        LONG X = (Rect.left + Rect.right) / 2, Y = (Rect.top + Rect.bottom) / 2;
        ok(ScreenColorNear(Screen, Origin, X, Y, Frames[i].Screen, &Actual),
           "Frame %u: damage of frame %u at %ld,%ld shows %06lx, expected %06lx\n",
           Last, i, X, Y, Actual, Frames[i].Screen);
    }
    for (UINT i = 0; i < ARRAYSIZE(Background); ++i)
    {
        ok(ScreenColorNear(Screen, Origin, Background[i].x, Background[i].y, Frames[0].Screen, &Actual),
           "Frame %u: undamaged pixel %ld,%ld shows %06lx, expected %06lx\n",
           Last, Background[i].x, Background[i].y, Actual, Frames[0].Screen);
    }
}

static HWND CreateTestWindow(POINT Origin)
{
    HWND Window = CreateWindowExW(WS_EX_TOPMOST | WS_EX_NOACTIVATE, L"STATIC", L"hwnd damage", WS_POPUP | WS_VISIBLE,
                                  Origin.x, Origin.y, WIDTH, HEIGHT, NULL, NULL, NULL, NULL);

    ok(Window != NULL, "CreateWindow: %lu\n", GetLastError());
    if (Window)
    {
        UpdateWindow(Window);
        Pump(100);
    }
    return Window;
}

static IDXGISwapChain1 *CreateChain(IDXGIFactory2 *Factory, ID3D11Device *Device, HWND Window, DXGI_SWAP_EFFECT Effect)
{
    DXGI_SWAP_CHAIN_DESC1 Desc = {};
    IDXGISwapChain1 *Chain = NULL;
    HRESULT Hr;

    Desc.Width = WIDTH;
    Desc.Height = HEIGHT;
    Desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    Desc.SampleDesc.Count = 1;
    Desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    Desc.BufferCount = Effect == DXGI_SWAP_EFFECT_DISCARD || Effect == DXGI_SWAP_EFFECT_SEQUENTIAL ? 1 : 2;
    Desc.Scaling = DXGI_SCALING_STRETCH;
    Desc.SwapEffect = Effect;
    Desc.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;
    Hr = Factory->CreateSwapChainForHwnd(Device, Window, &Desc, NULL, NULL, &Chain);
    ok(Hr == S_OK && Chain, "effect %u CreateSwapChainForHwnd: %#lx\n", Effect, Hr);
    return Chain;
}

static HRESULT PresentFrame(IDXGISwapChain1 *Chain, ID3D11DeviceContext *Context, UINT Frame)
{
    static DWORD Pixels[WIDTH * HEIGHT];
    ID3D11Texture2D *Buffer = NULL;
    RECT Damage = Frames[Frame].Rect;
    HRESULT Hr;

    Hr = Chain->GetBuffer(0, IID_ID3D11Texture2D, reinterpret_cast<void **>(&Buffer));
    ok(Hr == S_OK && Buffer, "Frame %u GetBuffer: %#lx\n", Frame, Hr);
    if (!Buffer)
        return Hr;
    for (UINT i = 0; i < ARRAYSIZE(Pixels); ++i) Pixels[i] = Frames[Frame].Pixel;
    D3D11_BOX Box = {static_cast<UINT>(Damage.left), static_cast<UINT>(Damage.top), 0,
                     static_cast<UINT>(Damage.right), static_cast<UINT>(Damage.bottom), 1};
    Context->UpdateSubresource(Buffer, 0, &Box, Pixels, WIDTH * sizeof(DWORD), 0);
    Buffer->Release();
    DXGI_PRESENT_PARAMETERS Params = {1, &Damage, NULL, NULL};
    return Chain->Present1(0, 0, &Params);
}

static void TestFlipSequential(IDXGIFactory2 *Factory, ID3D11Device *Device, ID3D11DeviceContext *Context, HDC Screen)
{
    POINT Origin = {200, 200};
    HWND Window = CreateTestWindow(Origin);
    IDXGISwapChain1 *Chain;

    if (!Window)
        return;
    Chain = CreateChain(Factory, Device, Window, DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL);
    for (UINT Frame = 0; Chain && Frame < ARRAYSIZE(Frames); ++Frame)
    {
        const RECT &Damage = Frames[Frame].Rect;
        HRESULT Hr = PresentFrame(Chain, Context, Frame);

        ok(Hr == S_OK, "Frame %u Present1: %#lx\n", Frame, Hr);
        if (FAILED(Hr))
            break;
        BOOL Shown = WaitForScreenColor(Screen, Origin, (Damage.left + Damage.right) / 2,
                                        (Damage.top + Damage.bottom) / 2, Frames[Frame].Screen);
        ok(Shown, "Frame %u never reached the screen\n", Frame);
        if (!Shown)
            break;
        CheckFrame(Screen, Origin, Frame);
    }
    if (Chain) Chain->Release();
    DestroyWindow(Window);
}

/* Discard effects refuse dirty rectangles, even one covering the whole buffer; bitblt SEQUENTIAL takes them. */
static void TestOtherEffects(IDXGIFactory2 *Factory, ID3D11Device *Device, ID3D11DeviceContext *Context)
{
    static const struct { DXGI_SWAP_EFFECT Effect; HRESULT Hr; } Effects[] =
    {
        { DXGI_SWAP_EFFECT_FLIP_DISCARD, DXGI_ERROR_INVALID_CALL },
        { DXGI_SWAP_EFFECT_DISCARD, DXGI_ERROR_INVALID_CALL },
        { DXGI_SWAP_EFFECT_SEQUENTIAL, S_OK },
    };
    POINT Origin = {400, 200};

    for (UINT Item = 0; Item < ARRAYSIZE(Effects); ++Item)
    {
        HWND Window = CreateTestWindow(Origin);
        IDXGISwapChain1 *Chain = Window ? CreateChain(Factory, Device, Window, Effects[Item].Effect) : NULL;

        if (Chain)
        {
            HRESULT Full = PresentFrame(Chain, Context, 0), Partial = PresentFrame(Chain, Context, 1);
            ok(Full == Effects[Item].Hr, "effect %u: full dirty rect %#lx\n", Effects[Item].Effect, Full);
            ok(Partial == Effects[Item].Hr, "effect %u: partial dirty rect %#lx\n", Effects[Item].Effect, Partial);
            Chain->Release();
        }
        if (Window) DestroyWindow(Window);
    }
}

START_TEST(hwnd_damage)
{
    HMODULE Runtime = LoadLibraryW(L"d3d11.dll");
    CREATE_DEVICE Create = Runtime ? reinterpret_cast<CREATE_DEVICE>(GetProcAddress(Runtime, "D3D11CreateDevice")) : NULL;
    ID3D11Device *Device = NULL;
    ID3D11DeviceContext *Context = NULL;
    IDXGIDevice *Dxgi = NULL;
    IDXGIAdapter *Adapter = NULL;
    IDXGIFactory2 *Factory = NULL;
    HRESULT Hr;

    if (!Create) { skip("D3D11 unavailable\n"); return; }
    Hr = Create(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, D3D11_CREATE_DEVICE_BGRA_SUPPORT, NULL, 0, D3D11_SDK_VERSION,
                &Device, NULL, &Context);
    if (FAILED(Hr)) { skip("Hardware D3D11 unavailable: %#lx\n", Hr); FreeLibrary(Runtime); return; }
    Hr = Device->QueryInterface(IID_IDXGIDevice, reinterpret_cast<void **>(&Dxgi));
    if (Dxgi) Hr = Dxgi->GetAdapter(&Adapter);
    if (Adapter) Hr = Adapter->GetParent(IID_IDXGIFactory2, reinterpret_cast<void **>(&Factory));
    ok(Hr == S_OK && Factory, "IDXGIFactory2: %#lx\n", Hr);
    HDC Screen = GetDC(NULL);
    if (Factory && Screen)
    {
        TestFlipSequential(Factory, Device, Context, Screen);
        TestOtherEffects(Factory, Device, Context);
    }
    if (Screen) ReleaseDC(NULL, Screen);
    if (Factory) Factory->Release();
    if (Adapter) Adapter->Release();
    if (Dxgi) Dxgi->Release();
    Context->ClearState();
    Context->Release();
    Device->Release();
    FreeLibrary(Runtime);
}
