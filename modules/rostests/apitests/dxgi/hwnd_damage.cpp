/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test that partial presents of window swap chains, with one or several dirty rectangles, keep undamaged pixels on screen, and that scrolled presents move the on-screen content
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

static void FillBuffer(ID3D11DeviceContext *Context, ID3D11Texture2D *Buffer, const RECT *Rect, DWORD Pixel)
{
    static DWORD Pixels[WIDTH * HEIGHT];
    RECT Area = {0, 0, WIDTH, HEIGHT};

    if (Rect)
        Area = *Rect;
    for (UINT i = 0; i < ARRAYSIZE(Pixels); ++i) Pixels[i] = Pixel;
    D3D11_BOX Box = {static_cast<UINT>(Area.left), static_cast<UINT>(Area.top), 0,
                     static_cast<UINT>(Area.right), static_cast<UINT>(Area.bottom), 1};
    Context->UpdateSubresource(Buffer, 0, &Box, Pixels, WIDTH * sizeof(DWORD), 0);
}

static DWORD ReadBufferPixel(ID3D11Device *Device, ID3D11DeviceContext *Context, ID3D11Texture2D *Buffer, LONG X, LONG Y)
{
    D3D11_TEXTURE2D_DESC Desc;
    ID3D11Texture2D *Staging = NULL;
    D3D11_MAPPED_SUBRESOURCE Mapped;
    DWORD Pixel = 0;

    Buffer->GetDesc(&Desc);
    Desc.Usage = D3D11_USAGE_STAGING;
    Desc.BindFlags = 0;
    Desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    Desc.MiscFlags = 0;
    if (FAILED(Device->CreateTexture2D(&Desc, NULL, &Staging)))
        return 0;
    Context->CopyResource(Staging, Buffer);
    if (SUCCEEDED(Context->Map(Staging, 0, D3D11_MAP_READ, 0, &Mapped)))
    {
        Pixel = static_cast<const DWORD *>(Mapped.pData)[Y * (Mapped.RowPitch / sizeof(DWORD)) + X];
        Context->Unmap(Staging, 0);
    }
    Staging->Release();
    return Pixel;
}

/* A present with several dirty rectangles updates only those rectangles; the gaps between them keep the screen. */
static void TestMultipleRects(IDXGIFactory2 *Factory, ID3D11Device *Device, ID3D11DeviceContext *Context, HDC Screen,
                              BOOL FillUndamaged)
{
    static const struct
    {
        RECT Damage[2];
        DWORD Pixel[2];
        COLORREF Screen[2];
        DWORD Background;
    } Presents[] =
    {
        {{{8, 8, 40, 32}, {88, 56, 120, 88}}, {0xff00ff00, 0xff0000ff}, {RGB(0, 255, 0), RGB(0, 0, 255)}, 0xffffffff},
        {{{8, 56, 40, 88}, {88, 8, 120, 32}}, {0xffffff00, 0xff00ffff}, {RGB(255, 255, 0), RGB(0, 255, 255)}, 0xffff00ff},
        {{{44, 36, 60, 52}, {68, 44, 84, 60}}, {0xff000000, 0xffff8000}, {RGB(0, 0, 0), RGB(255, 128, 0)}, 0xff808080},
    };
    static const POINT Gaps[] = {{4, 46}, {64, 4}, {64, 92}, {124, 46}, {42, 70}, {100, 40}};
    POINT Origin = {FillUndamaged ? 800 : 600, 200};
    HWND Window = CreateTestWindow(Origin);
    IDXGISwapChain1 *Chain = Window ? CreateChain(Factory, Device, Window, DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL) : NULL;
    ID3D11Texture2D *Buffer = NULL;
    COLORREF Actual;
    HRESULT Hr;

    if (!Chain)
    {
        if (Window) DestroyWindow(Window);
        return;
    }

    Hr = Chain->GetBuffer(0, IID_ID3D11Texture2D, reinterpret_cast<void **>(&Buffer));
    ok(Hr == S_OK && Buffer, "GetBuffer: %#lx\n", Hr);
    if (Buffer)
    {
        RECT Full = {0, 0, WIDTH, HEIGHT};
        DXGI_PRESENT_PARAMETERS Params = {1, &Full, NULL, NULL};

        FillBuffer(Context, Buffer, NULL, Frames[0].Pixel);
        Buffer->Release();
        Hr = Chain->Present1(0, 0, &Params);
        ok(Hr == S_OK, "Initial Present1: %#lx\n", Hr);
        ok(WaitForScreenColor(Screen, Origin, 64, 44, Frames[0].Screen), "Initial frame never reached the screen\n");
    }

    for (UINT Present = 0; Buffer && Present < ARRAYSIZE(Presents); ++Present)
    {
        DXGI_PRESENT_PARAMETERS Params = {2, const_cast<RECT *>(Presents[Present].Damage), NULL, NULL};
        ID3D11Texture2D *Back = NULL;

        Hr = Chain->GetBuffer(0, IID_ID3D11Texture2D, reinterpret_cast<void **>(&Back));
        ok(Hr == S_OK && Back, "Present %u GetBuffer: %#lx\n", Present, Hr);
        if (!Back)
            break;
        if (FillUndamaged)
            FillBuffer(Context, Back, NULL, Presents[Present].Background);
        for (UINT i = 0; i < 2; ++i)
            FillBuffer(Context, Back, &Presents[Present].Damage[i], Presents[Present].Pixel[i]);
        Back->Release();
        Hr = Chain->Present1(0, 0, &Params);
        ok(Hr == S_OK, "Present %u Present1: %#lx\n", Present, Hr);
        if (FAILED(Hr))
            break;

        for (UINT i = 0; i < 2; ++i)
        {
            const RECT &Rect = Presents[Present].Damage[i];
            ok(WaitForScreenColor(Screen, Origin, (Rect.left + Rect.right) / 2, (Rect.top + Rect.bottom) / 2,
                                  Presents[Present].Screen[i]),
               "Present %u: dirty rect %u never reached the screen\n", Present, i);
        }
        for (UINT i = 0; i < ARRAYSIZE(Gaps); ++i)
        {
            BOOL Inside = FALSE;

            for (UINT Past = 0; Past <= Present; ++Past)
                for (UINT j = 0; j < 2; ++j)
                    if (PtInRect(&Presents[Past].Damage[j], Gaps[i]))
                        Inside = TRUE;
            if (Inside)
                continue;
            if (FillUndamaged)
            {
                ScreenColorNear(Screen, Origin, Gaps[i].x, Gaps[i].y, Frames[0].Screen, &Actual);
                trace("Present %u: filled gap pixel %ld,%ld shows %06lx\n", Present, Gaps[i].x, Gaps[i].y, Actual);
                continue;
            }
            ok(ScreenColorNear(Screen, Origin, Gaps[i].x, Gaps[i].y, Frames[0].Screen, &Actual),
               "Present %u: gap pixel %ld,%ld shows %06lx, expected %06lx\n",
               Present, Gaps[i].x, Gaps[i].y, Actual, Frames[0].Screen);
        }

        Hr = Chain->GetBuffer(0, IID_ID3D11Texture2D, reinterpret_cast<void **>(&Back));
        if (SUCCEEDED(Hr))
        {
            trace("Present %u: buffer 0 afterwards holds %08lx at 64,4, presented background was %08lx\n",
                  Present, ReadBufferPixel(Device, Context, Back, 64, 4), Presents[Present].Background);
            Back->Release();
        }
    }

    Chain->Release();
    DestroyWindow(Window);
}

#define STRIPE_COUNT 12
#define STRIPE_HEIGHT (HEIGHT / STRIPE_COUNT)

static COLORREF PixelToColor(DWORD Pixel)
{
    return RGB((Pixel >> 16) & 0xff, (Pixel >> 8) & 0xff, Pixel & 0xff);
}

/* A scrolled present moves the on-screen content by the offset; the application only draws the exposed strip. */
static void TestScroll(IDXGIFactory2 *Factory, ID3D11Device *Device, ID3D11DeviceContext *Context, HDC Screen)
{
    static const DWORD Initial[STRIPE_COUNT] =
    {
        0xffff0000, 0xff00ff00, 0xff0000ff, 0xffffff00, 0xffff00ff, 0xff00ffff,
        0xff800000, 0xff008000, 0xff000080, 0xff808000, 0xff800080, 0xff008080,
    };
    static const DWORD Exposed[] = {0xffffffff, 0xff000000};
    POINT Origin = {200, 400};
    HWND Window = CreateTestWindow(Origin);
    IDXGISwapChain1 *Chain = Window ? CreateChain(Factory, Device, Window, DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL) : NULL;
    ID3D11Texture2D *Buffer = NULL;
    DWORD Current[STRIPE_COUNT];
    COLORREF Actual;
    HRESULT Hr;

    if (!Chain)
    {
        if (Window) DestroyWindow(Window);
        return;
    }
    memcpy(Current, Initial, sizeof(Current));

    Hr = Chain->GetBuffer(0, IID_ID3D11Texture2D, reinterpret_cast<void **>(&Buffer));
    ok(Hr == S_OK && Buffer, "GetBuffer: %#lx\n", Hr);
    if (Buffer)
    {
        RECT Full = {0, 0, WIDTH, HEIGHT};
        DXGI_PRESENT_PARAMETERS Params = {1, &Full, NULL, NULL};

        for (UINT i = 0; i < STRIPE_COUNT; ++i)
        {
            RECT Stripe = {0, static_cast<LONG>(i * STRIPE_HEIGHT), WIDTH, static_cast<LONG>((i + 1) * STRIPE_HEIGHT)};
            FillBuffer(Context, Buffer, &Stripe, Initial[i]);
        }
        Buffer->Release();
        Hr = Chain->Present1(0, 0, &Params);
        ok(Hr == S_OK, "Initial Present1: %#lx\n", Hr);
        ok(WaitForScreenColor(Screen, Origin, 64, HEIGHT - 2, PixelToColor(Initial[STRIPE_COUNT - 1])),
           "Initial frame never reached the screen\n");
    }

    for (UINT Step = 0; Buffer && Step < ARRAYSIZE(Exposed); ++Step)
    {
        static const UINT Shift = 2;
        RECT Dirty = {0, static_cast<LONG>(HEIGHT - Shift * STRIPE_HEIGHT), WIDTH, HEIGHT};
        RECT Scroll = {0, 0, WIDTH, static_cast<LONG>(HEIGHT - Shift * STRIPE_HEIGHT)};
        POINT Offset = {0, -static_cast<LONG>(Shift * STRIPE_HEIGHT)};
        DXGI_PRESENT_PARAMETERS Params = {1, &Dirty, &Scroll, &Offset};
        ID3D11Texture2D *Back = NULL;

        Hr = Chain->GetBuffer(0, IID_ID3D11Texture2D, reinterpret_cast<void **>(&Back));
        ok(Hr == S_OK && Back, "Scroll %u GetBuffer: %#lx\n", Step, Hr);
        if (!Back)
            break;
        FillBuffer(Context, Back, NULL, 0xff404040);
        FillBuffer(Context, Back, &Dirty, Exposed[Step]);
        Back->Release();
        Hr = Chain->Present1(0, 0, &Params);
        ok(Hr == S_OK, "Scroll %u Present1: %#lx\n", Step, Hr);
        if (FAILED(Hr))
            break;

        for (UINT i = 0; i + Shift < STRIPE_COUNT; ++i)
            Current[i] = Current[i + Shift];
        for (UINT i = STRIPE_COUNT - Shift; i < STRIPE_COUNT; ++i)
            Current[i] = Exposed[Step];

        ok(WaitForScreenColor(Screen, Origin, 64, HEIGHT - 2, PixelToColor(Exposed[Step])),
           "Scroll %u: exposed strip never reached the screen\n", Step);
        for (UINT i = 0; i < STRIPE_COUNT; ++i)
        {
            LONG Y = static_cast<LONG>(i * STRIPE_HEIGHT + STRIPE_HEIGHT / 2);
            ok(ScreenColorNear(Screen, Origin, 64, Y, PixelToColor(Current[i]), &Actual),
               "Scroll %u: stripe %u at y %ld shows %06lx, expected %06lx\n",
               Step, i, Y, Actual, PixelToColor(Current[i]));
        }
    }

    if (Buffer)
    {
        static const struct { RECT Scroll; POINT Offset; } Invalid[] =
        {
            {{0, 0, WIDTH, 80}, {0, 16}},
            {{0, 0, 0, 0}, {0, -16}},
            {{0, 0, WIDTH + 8, 80}, {0, -16}},
            {{0, 0, WIDTH, 80}, {-16, -16}},
        };
        RECT Dirty = {0, 80, WIDTH, HEIGHT};

        for (UINT i = 0; i < ARRAYSIZE(Invalid); ++i)
        {
            DXGI_PRESENT_PARAMETERS Params = {1, &Dirty, const_cast<RECT *>(&Invalid[i].Scroll),
                                              const_cast<POINT *>(&Invalid[i].Offset)};

            Hr = Chain->Present1(0, 0, &Params);
            ok(Hr == DXGI_ERROR_INVALID_CALL, "Invalid scroll %u: Present1 %#lx\n", i, Hr);
        }
    }

    Chain->Release();
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
            RECT Scroll = {0, 0, WIDTH, HEIGHT - 16};
            POINT Offset = {0, -16};
            DXGI_PRESENT_PARAMETERS Params = {0, NULL, &Scroll, &Offset};
            HRESULT Scrolled = Chain->Present1(0, 0, &Params);
            ok(Scrolled == DXGI_ERROR_INVALID_CALL, "effect %u: scroll %#lx\n", Effects[Item].Effect, Scrolled);
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
    SetProcessDPIAware();
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
        TestMultipleRects(Factory, Device, Context, Screen, FALSE);
        TestMultipleRects(Factory, Device, Context, Screen, TRUE);
        TestScroll(Factory, Device, Context, Screen);
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
