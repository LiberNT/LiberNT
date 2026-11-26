/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests WS_EX_NOREDIRECTIONBITMAP creation semantics and composition transparency
 * COPYRIGHT:   Copyright 2026 Earzu Chan <contact@earzuchan.me>
 */

#define COBJMACROS
#include <apitest.h>
#include <initguid.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <dcomp.h>

typedef HRESULT (WINAPI *PD3D11CREATEDEVICE)(IDXGIAdapter *, D3D_DRIVER_TYPE, HMODULE, UINT,
                                             const D3D_FEATURE_LEVEL *, UINT, UINT, ID3D11Device **,
                                             D3D_FEATURE_LEVEL *, ID3D11DeviceContext **);
typedef HRESULT (WINAPI *PDCOMPOSITIONCREATEDEVICE)(IDXGIDevice *, REFIID, void **);

#define CHAIN_WIDTH 160
#define CHAIN_HEIGHT 48
#define BAND_WIDTH 48
#define BAND_B_X 56
#define BAND_C_X 112
#define VISUAL_X 8
#define VISUAL_Y 8

static LRESULT CALLBACK
NoRedirectionDefProc(
    _In_ HWND Window,
    _In_ UINT Message,
    _In_ WPARAM wParam,
    _In_ LPARAM lParam)
{
    return DefWindowProcW(Window, Message, wParam, lParam);
}

static LRESULT CALLBACK
NoRedirectionTopProc(
    _In_ HWND Window,
    _In_ UINT Message,
    _In_ WPARAM wParam,
    _In_ LPARAM lParam)
{
    if (Message == WM_ERASEBKGND)
    {
        RECT Rect;

        GetClientRect(Window, &Rect);
        FillRect((HDC)wParam, &Rect, (HBRUSH)GetStockObject(WHITE_BRUSH));
        return 1;
    }
    if (Message == WM_PAINT)
    {
        PAINTSTRUCT Paint;
        HBRUSH Brush;
        RECT Rect;

        BeginPaint(Window, &Paint);
        GetClientRect(Window, &Rect);
        Brush = CreateSolidBrush(RGB(0, 0, 255));
        FillRect(Paint.hdc, &Rect, Brush);
        DeleteObject(Brush);
        EndPaint(Window, &Paint);
        return 0;
    }
    return DefWindowProcW(Window, Message, wParam, lParam);
}

static BOOL
ColorNear(
    _In_ COLORREF Color,
    _In_ BYTE Red,
    _In_ BYTE Green,
    _In_ BYTE Blue,
    _In_ BYTE Tolerance)
{
    int RedDelta = (int)GetRValue(Color) - Red;
    int GreenDelta = (int)GetGValue(Color) - Green;
    int BlueDelta = (int)GetBValue(Color) - Blue;

    return RedDelta >= -Tolerance && RedDelta <= Tolerance &&
           GreenDelta >= -Tolerance && GreenDelta <= Tolerance &&
           BlueDelta >= -Tolerance && BlueDelta <= Tolerance;
}

static COLORREF
ScreenPixel(
    _In_ HWND Window,
    _In_ int X,
    _In_ int Y)
{
    POINT Point = {X, Y};
    COLORREF Color;
    HDC Screen;

    ClientToScreen(Window, &Point);
    Screen = GetDC(NULL);
    Color = GetPixel(Screen, Point.x, Point.y);
    ReleaseDC(NULL, Screen);
    return Color;
}

static void
Pump(
    _In_ DWORD Milliseconds)
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
        Sleep(20);
    }
}

typedef BOOL (WINAPI *PSETPROCESSDPIAWARENESSCONTEXT)(DPI_AWARENESS_CONTEXT);

static void
UsePhysicalPixels(void)
{
    PSETPROCESSDPIAWARENESSCONTEXT SetAwareness;

    SetAwareness = (PSETPROCESSDPIAWARENESSCONTEXT)GetProcAddress(GetModuleHandleW(L"user32.dll"),
                                                                  "SetProcessDpiAwarenessContext");
    if (!SetAwareness || !SetAwareness(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2))
        SetProcessDPIAware();
}

START_TEST(noredirection_style)
{
    static const WCHAR ClassName[] = L"NoRedirectionStyle";
    WNDCLASSW Class = {0};
    DWORD NoRedirectionStyle, NormalStyle, LayeredStyle;
    DWORD PreviousStyle, ResultStyle, Error;
    HWND NoRedirection, Normal, Layered;
    ATOM ClassAtom;
    BOOL Success;
    HDC WindowDc;
    int Released;

    UsePhysicalPixels();
    Class.lpfnWndProc = NoRedirectionDefProc;
    Class.hInstance = GetModuleHandleW(NULL);
    Class.lpszClassName = ClassName;
    SetLastError(0);
    ClassAtom = RegisterClassW(&Class);
    Error = GetLastError();
    trace("style class registration: atom %#x error %lu\n", ClassAtom, Error);
    ok(ClassAtom != 0, "RegisterClassW failed, atom %#x error %lu\n", ClassAtom, Error);
    if (!ClassAtom)
        return;

    SetLastError(0);
    NoRedirection = CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP, ClassName, L"no redirection",
                                    WS_POPUP, 0, 0, 100, 100, NULL, NULL, Class.hInstance, NULL);
    Error = GetLastError();
    NoRedirectionStyle = NoRedirection ? (DWORD)GetWindowLongW(NoRedirection, GWL_EXSTYLE) : 0;
    trace("no-redirection create: hwnd %p error %lu exstyle %#lx\n", NoRedirection, Error, NoRedirectionStyle);
    ok(NoRedirection != NULL, "no-redirection CreateWindowExW returned %p error %lu\n", NoRedirection, Error);
    ok((NoRedirectionStyle & WS_EX_NOREDIRECTIONBITMAP) != 0,
       "no-redirection creation style is %#lx\n", NoRedirectionStyle);

    if (NoRedirection)
    {
        NoRedirectionStyle = (DWORD)GetWindowLongW(NoRedirection, GWL_EXSTYLE);
        SetLastError(0);
        PreviousStyle = (DWORD)SetWindowLongW(NoRedirection, GWL_EXSTYLE,
                                              (LONG)(NoRedirectionStyle & ~WS_EX_NOREDIRECTIONBITMAP));
        Error = GetLastError();
        ResultStyle = (DWORD)GetWindowLongW(NoRedirection, GWL_EXSTYLE);
        trace("no-redirection clear: previous %#lx error %lu resulting %#lx\n", PreviousStyle, Error, ResultStyle);
        ok(PreviousStyle == 0 && Error == ERROR_INVALID_PARAMETER,
           "clearing no-redirection returned %#lx, error %lu\n", PreviousStyle, Error);
        ok((ResultStyle & WS_EX_NOREDIRECTIONBITMAP) != 0,
           "cleared no-redirection style is %#lx (SetWindowLongW returned %#lx, error %lu)\n",
           ResultStyle, PreviousStyle, Error);

        NoRedirectionStyle = ResultStyle;
        SetLastError(0);
        PreviousStyle = (DWORD)SetWindowLongW(NoRedirection, GWL_EXSTYLE,
                                              (LONG)(NoRedirectionStyle | WS_EX_TOOLWINDOW));
        Error = GetLastError();
        ResultStyle = (DWORD)GetWindowLongW(NoRedirection, GWL_EXSTYLE);
        trace("no-redirection keep + toolwindow: previous %#lx error %lu resulting %#lx\n",
              PreviousStyle, Error, ResultStyle);
        ok(PreviousStyle == NoRedirectionStyle,
           "keeping no-redirection returned %#lx, error %lu\n", PreviousStyle, Error);
        ok((ResultStyle & (WS_EX_NOREDIRECTIONBITMAP | WS_EX_TOOLWINDOW)) ==
               (WS_EX_NOREDIRECTIONBITMAP | WS_EX_TOOLWINDOW),
           "kept no-redirection style is %#lx\n", ResultStyle);

        NoRedirectionStyle = ResultStyle;
        SetLastError(0);
        PreviousStyle = (DWORD)SetWindowLongW(NoRedirection, GWL_EXSTYLE,
                                              (LONG)(NoRedirectionStyle & ~(WS_EX_NOREDIRECTIONBITMAP | WS_EX_TOOLWINDOW)));
        Error = GetLastError();
        ResultStyle = (DWORD)GetWindowLongW(NoRedirection, GWL_EXSTYLE);
        trace("no-redirection clear + toolwindow clear: previous %#lx error %lu resulting %#lx\n",
              PreviousStyle, Error, ResultStyle);
        ok(PreviousStyle == 0 && Error == ERROR_INVALID_PARAMETER,
           "clearing no-redirection with another bit returned %#lx, error %lu\n", PreviousStyle, Error);
        ok(ResultStyle == NoRedirectionStyle,
           "style after a rejected combined change is %#lx, expected %#lx\n", ResultStyle, NoRedirectionStyle);
    }

    SetLastError(0);
    Normal = CreateWindowExW(0, ClassName, L"normal", WS_POPUP, 0, 0, 100, 100,
                             NULL, NULL, Class.hInstance, NULL);
    Error = GetLastError();
    NormalStyle = Normal ? (DWORD)GetWindowLongW(Normal, GWL_EXSTYLE) : 0;
    trace("normal create: hwnd %p error %lu exstyle %#lx\n", Normal, Error, NormalStyle);
    ok(Normal != NULL, "normal CreateWindowExW returned %p error %lu\n", Normal, Error);

    if (Normal)
    {
        NormalStyle = (DWORD)GetWindowLongW(Normal, GWL_EXSTYLE);
        SetLastError(0);
        PreviousStyle = (DWORD)SetWindowLongW(Normal, GWL_EXSTYLE,
                                              (LONG)(NormalStyle | WS_EX_NOREDIRECTIONBITMAP));
        Error = GetLastError();
        ResultStyle = (DWORD)GetWindowLongW(Normal, GWL_EXSTYLE);
        trace("normal add: previous %#lx error %lu resulting %#lx\n", PreviousStyle, Error, ResultStyle);
        ok(PreviousStyle == 0 && Error == ERROR_INVALID_PARAMETER,
           "adding no-redirection returned %#lx, error %lu\n", PreviousStyle, Error);
        ok((ResultStyle & WS_EX_NOREDIRECTIONBITMAP) == 0,
           "added no-redirection style is %#lx (SetWindowLongW returned %#lx, error %lu)\n",
           ResultStyle, PreviousStyle, Error);

        SetLastError(0);
        PreviousStyle = (DWORD)SetWindowLongPtrW(Normal, GWL_EXSTYLE,
                                                 (LONG_PTR)(NormalStyle | WS_EX_NOREDIRECTIONBITMAP | WS_EX_TOOLWINDOW));
        Error = GetLastError();
        ResultStyle = (DWORD)GetWindowLongW(Normal, GWL_EXSTYLE);
        trace("normal add + toolwindow (ptr): previous %#lx error %lu resulting %#lx\n",
              PreviousStyle, Error, ResultStyle);
        ok(PreviousStyle == 0 && Error == ERROR_INVALID_PARAMETER,
           "adding no-redirection with another bit returned %#lx, error %lu\n", PreviousStyle, Error);
        ok(ResultStyle == NormalStyle,
           "style after a rejected combined change is %#lx, expected %#lx\n", ResultStyle, NormalStyle);
    }

    SetLastError(0);
    Layered = CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP | WS_EX_LAYERED, ClassName, L"layered",
                              WS_POPUP, 0, 0, 100, 100, NULL, NULL, Class.hInstance, NULL);
    Error = GetLastError();
    LayeredStyle = Layered ? (DWORD)GetWindowLongW(Layered, GWL_EXSTYLE) : 0;
    trace("layered create: hwnd %p error %lu exstyle %#lx\n", Layered, Error, LayeredStyle);
    ok(Layered != NULL, "layered CreateWindowExW returned %p error %lu\n", Layered, Error);
    ok((LayeredStyle & WS_EX_NOREDIRECTIONBITMAP) != 0 && (LayeredStyle & WS_EX_LAYERED) != 0,
       "layered creation style is %#lx\n", LayeredStyle);

    if (Layered)
    {
        SetLastError(0);
        Success = SetLayeredWindowAttributes(Layered, 0, 128, LWA_ALPHA);
        Error = GetLastError();
        trace("layered attributes: result %d error %lu\n", Success, Error);
        ok(Success, "SetLayeredWindowAttributes returned %d error %lu\n", Success, Error);
    }

    if (NoRedirection)
    {
        SetLastError(0);
        WindowDc = GetDC(NoRedirection);
        Error = GetLastError();
        trace("no-redirection GetDC: dc %p error %lu\n", WindowDc, Error);
        ok(WindowDc != NULL, "GetDC returned %p error %lu\n", WindowDc, Error);
        if (WindowDc)
        {
            Released = ReleaseDC(NoRedirection, WindowDc);
            trace("no-redirection ReleaseDC: result %d\n", Released);
        }
    }

    if (Layered)
        DestroyWindow(Layered);
    if (Normal)
        DestroyWindow(Normal);
    if (NoRedirection)
        DestroyWindow(NoRedirection);
    UnregisterClassW(ClassName, Class.hInstance);
}

START_TEST(noredirection_transparency)
{
    static const WCHAR BottomClassName[] = L"NoRedirBottom";
    static const WCHAR TopClassName[] = L"NoRedirTop";
    DWORD Pixels[CHAIN_WIDTH * CHAIN_HEIGHT] = {0};
    DXGI_SWAP_CHAIN_DESC1 Desc = {0};
    PDCOMPOSITIONCREATEDEVICE CreateComposition;
    PD3D11CREATEDEVICE CreateDevice;
    IDCompositionDevice *Composition = NULL;
    IDCompositionTarget *Target = NULL;
    IDCompositionVisual *Visual = NULL;
    ID3D11DeviceContext *Context = NULL;
    ID3D11Texture2D *Buffer = NULL;
    IDXGISwapChain1 *SwapChain = NULL;
    IDXGIFactory2 *Factory = NULL;
    IDXGIAdapter *Adapter = NULL;
    IDXGIDevice *DxgiDevice = NULL;
    ID3D11Device *Device = NULL;
    HBRUSH GreenBrush = NULL;
    HMODULE D3d11 = NULL, Dcomp = NULL;
    WNDCLASSW BottomClass = {0}, TopClass = {0};
    HWND Bottom = NULL, Top = NULL;
    COLORREF PixelA, PixelB, PixelC, Uncovered;
    ATOM BottomAtom, TopAtom;
    HINSTANCE Instance;
    HRESULT hr;
    UINT X, Y;

    for (Y = 0; Y < CHAIN_HEIGHT; Y++)
    {
        for (X = 0; X < BAND_WIDTH; X++)
        {
            Pixels[Y * CHAIN_WIDTH + BAND_B_X + X] = 0x80800000;
            Pixels[Y * CHAIN_WIDTH + BAND_C_X + X] = 0xffff0000;
        }
    }

    UsePhysicalPixels();
    Instance = GetModuleHandleW(NULL);
    GreenBrush = CreateSolidBrush(RGB(0, 255, 0));
    BottomClass.lpfnWndProc = NoRedirectionDefProc;
    BottomClass.hInstance = Instance;
    BottomClass.hbrBackground = GreenBrush;
    BottomClass.lpszClassName = BottomClassName;
    BottomAtom = RegisterClassW(&BottomClass);
    TopClass.lpfnWndProc = NoRedirectionTopProc;
    TopClass.hInstance = Instance;
    TopClass.hbrBackground = (HBRUSH)GetStockObject(WHITE_BRUSH);
    TopClass.lpszClassName = TopClassName;
    TopAtom = RegisterClassW(&TopClass);
    ok(BottomAtom != 0, "bottom RegisterClassW failed, error %lu\n", GetLastError());
    ok(TopAtom != 0, "top RegisterClassW failed, error %lu\n", GetLastError());
    if (!BottomAtom || !TopAtom)
        goto Cleanup;

    Bottom = CreateWindowExW(WS_EX_TOPMOST, BottomClassName, L"bottom", WS_POPUP | WS_VISIBLE,
                             100, 100, 240, 120, NULL, NULL, Instance, NULL);
    ok(Bottom != NULL, "bottom CreateWindowExW failed, error %lu\n", GetLastError());
    if (!Bottom)
        goto Cleanup;
    UpdateWindow(Bottom);
    Pump(200);

    Top = CreateWindowExW(WS_EX_TOPMOST | WS_EX_NOREDIRECTIONBITMAP, TopClassName, L"top",
                          WS_POPUP | WS_VISIBLE, 100, 100, 240, 120, NULL, NULL, Instance, NULL);
    ok(Top != NULL, "top CreateWindowExW failed, error %lu\n", GetLastError());
    if (!Top)
        goto Cleanup;
    SetWindowPos(Top, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
    SetForegroundWindow(Top);
    UpdateWindow(Top);
    Pump(200);

    D3d11 = LoadLibraryW(L"d3d11.dll");
    Dcomp = LoadLibraryW(L"dcomp.dll");
    CreateDevice = D3d11 ? (PD3D11CREATEDEVICE)GetProcAddress(D3d11, "D3D11CreateDevice") : NULL;
    CreateComposition = Dcomp ? (PDCOMPOSITIONCREATEDEVICE)GetProcAddress(Dcomp, "DCompositionCreateDevice") : NULL;
    if (!CreateDevice || !CreateComposition)
    {
        skip("d3d11 or dcomp is unavailable\n");
        goto Cleanup;
    }

    hr = CreateDevice(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, D3D11_CREATE_DEVICE_BGRA_SUPPORT, NULL, 0,
                      D3D11_SDK_VERSION, &Device, NULL, &Context);
    if (FAILED(hr))
    {
        skip("D3D11CreateDevice failed, hr %#lx\n", hr);
        goto Cleanup;
    }
    hr = ID3D11Device_QueryInterface(Device, &IID_IDXGIDevice, (void **)&DxgiDevice);
    if (SUCCEEDED(hr))
        hr = IDXGIDevice_GetAdapter(DxgiDevice, &Adapter);
    if (SUCCEEDED(hr))
        hr = IDXGIAdapter_GetParent(Adapter, &IID_IDXGIFactory2, (void **)&Factory);
    ok(hr == S_OK, "IDXGIFactory2 lookup failed, hr %#lx\n", hr);

    if (SUCCEEDED(hr))
    {
        Desc.Width = CHAIN_WIDTH;
        Desc.Height = CHAIN_HEIGHT;
        Desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        Desc.SampleDesc.Count = 1;
        Desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        Desc.BufferCount = 2;
        Desc.Scaling = DXGI_SCALING_STRETCH;
        Desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
        Desc.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;
        hr = IDXGIFactory2_CreateSwapChainForComposition(Factory, (IUnknown *)Device, &Desc, NULL, &SwapChain);
        ok(hr == S_OK, "CreateSwapChainForComposition failed, hr %#lx\n", hr);
    }
    if (SUCCEEDED(hr))
        hr = IDXGISwapChain1_GetBuffer(SwapChain, 0, &IID_ID3D11Texture2D, (void **)&Buffer);
    ok(hr == S_OK, "swap chain buffer lookup failed, hr %#lx\n", hr);
    if (SUCCEEDED(hr))
    {
        ID3D11DeviceContext_UpdateSubresource(Context, (ID3D11Resource *)Buffer, 0, NULL, Pixels,
                                              CHAIN_WIDTH * sizeof(DWORD), 0);
        hr = IDXGISwapChain1_Present(SwapChain, 0, 0);
        trace("initial present returned %#lx\n", hr);
        ok(hr == S_OK, "initial Present failed, hr %#lx\n", hr);
    }

    if (SUCCEEDED(hr))
        hr = CreateComposition(DxgiDevice, &IID_IDCompositionDevice, (void **)&Composition);
    if (SUCCEEDED(hr))
        hr = IDCompositionDevice_CreateTargetForHwnd(Composition, Top, TRUE, &Target);
    if (SUCCEEDED(hr))
        hr = IDCompositionDevice_CreateVisual(Composition, &Visual);
    if (SUCCEEDED(hr))
        hr = IDCompositionVisual_SetContent(Visual, (IUnknown *)SwapChain);
    if (SUCCEEDED(hr))
        hr = IDCompositionVisual_SetOffsetX(Visual, (float)VISUAL_X);
    if (SUCCEEDED(hr))
        hr = IDCompositionVisual_SetOffsetY(Visual, (float)VISUAL_Y);
    if (SUCCEEDED(hr))
        hr = IDCompositionTarget_SetRoot(Target, Visual);
    if (SUCCEEDED(hr))
        hr = IDCompositionDevice_Commit(Composition);
    ok(hr == S_OK, "composition setup failed, hr %#lx\n", hr);

    if (SUCCEEDED(hr))
    {
        ID3D11DeviceContext_UpdateSubresource(Context, (ID3D11Resource *)Buffer, 0, NULL, Pixels,
                                              CHAIN_WIDTH * sizeof(DWORD), 0);
        hr = IDXGISwapChain1_Present(SwapChain, 0, 0);
        trace("composition present returned %#lx\n", hr);
    }
    if (SUCCEEDED(hr))
        hr = IDCompositionDevice_WaitForCommitCompletion(Composition);
    ok(hr == S_OK, "composition completion failed, hr %#lx\n", hr);

    if (SUCCEEDED(hr))
    {
        Pump(500);
        PixelA = ScreenPixel(Top, VISUAL_X + 24, VISUAL_Y + 24);
        PixelB = ScreenPixel(Top, VISUAL_X + 80, VISUAL_Y + 24);
        PixelC = ScreenPixel(Top, VISUAL_X + 136, VISUAL_Y + 24);
        Uncovered = ScreenPixel(Top, 200, 90);
        ok(ColorNear(PixelA, 0, 255, 0, 8),
           "transparent band pixel is %#lx (r %u g %u b %u)\n",
           PixelA, GetRValue(PixelA), GetGValue(PixelA), GetBValue(PixelA));
        ok(ColorNear(PixelB, 128, 128, 0, 8),
           "half-red band pixel is %#lx (r %u g %u b %u)\n",
           PixelB, GetRValue(PixelB), GetGValue(PixelB), GetBValue(PixelB));
        ok(ColorNear(PixelC, 255, 0, 0, 8),
           "opaque red band pixel is %#lx (r %u g %u b %u)\n",
           PixelC, GetRValue(PixelC), GetGValue(PixelC), GetBValue(PixelC));
        ok(ColorNear(Uncovered, 0, 255, 0, 8),
           "uncovered pixel is %#lx (r %u g %u b %u)\n",
           Uncovered, GetRValue(Uncovered), GetGValue(Uncovered), GetBValue(Uncovered));
    }

Cleanup:
    if (Visual)
        IDCompositionVisual_Release(Visual);
    if (Target)
        IDCompositionTarget_Release(Target);
    if (Composition)
        IDCompositionDevice_Release(Composition);
    if (Buffer)
        ID3D11Texture2D_Release(Buffer);
    if (SwapChain)
        IDXGISwapChain1_Release(SwapChain);
    if (Factory)
        IDXGIFactory2_Release(Factory);
    if (Adapter)
        IDXGIAdapter_Release(Adapter);
    if (DxgiDevice)
        IDXGIDevice_Release(DxgiDevice);
    if (Context)
        ID3D11DeviceContext_Release(Context);
    if (Device)
        ID3D11Device_Release(Device);
    if (Top)
        DestroyWindow(Top);
    if (Bottom)
        DestroyWindow(Bottom);
    if (TopAtom)
        UnregisterClassW(TopClassName, Instance);
    if (BottomAtom)
        UnregisterClassW(BottomClassName, Instance);
    if (GreenBrush)
        DeleteObject(GreenBrush);
    if (Dcomp)
        FreeLibrary(Dcomp);
    if (D3d11)
        FreeLibrary(D3d11);
}
