/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests that composition content and window DCs reach the screen after another process changes the display mode
 * COPYRIGHT:   Copyright 2026 mikuta0407 <mikuta0407@mikuta0407.net>
 */

#define COBJMACROS
#include <apitest.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <dcomp.h>
#include <stdio.h>
#include <stdlib.h>

typedef HRESULT (WINAPI *PD3D11CREATEDEVICE)(IDXGIAdapter *, D3D_DRIVER_TYPE, HMODULE, UINT,
                                             const D3D_FEATURE_LEVEL *, UINT, UINT, ID3D11Device **,
                                             D3D_FEATURE_LEVEL *, ID3D11DeviceContext **);
typedef HRESULT (WINAPI *PDCOMPOSITIONCREATEDEVICE)(IDXGIDevice *, REFIID, void **);

#define MODE_WINDOW_WIDTH 200
#define MODE_WINDOW_HEIGHT 120
#define MODE_VISUAL_X 40
#define MODE_VISUAL_Y 30
#define MODE_VISUAL_WIDTH 64
#define MODE_VISUAL_HEIGHT 32
#define MODE_GDI_X 4
#define MODE_GDI_Y 4
#define MODE_GDI_SIZE 24

static BOOL
MatchesColor(
    _In_ COLORREF Color,
    _In_ BYTE Red,
    _In_ BYTE Green,
    _In_ BYTE Blue)
{
    return abs(GetRValue(Color) - Red) <= 40 &&
           abs(GetGValue(Color) - Green) <= 40 &&
           abs(GetBValue(Color) - Blue) <= 40;
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

static BOOL
FindOtherMode(
    _Out_ DEVMODEW *Current,
    _Out_ DEVMODEW *Other)
{
    DEVMODEW Mode;
    DWORD Index;

    ZeroMemory(Current, sizeof(*Current));
    ZeroMemory(Other, sizeof(*Other));
    Current->dmSize = sizeof(*Current);
    if (!EnumDisplaySettingsW(NULL, ENUM_CURRENT_SETTINGS, Current))
        return FALSE;
    for (Index = 0;; ++Index)
    {
        ZeroMemory(&Mode, sizeof(Mode));
        Mode.dmSize = sizeof(Mode);
        if (!EnumDisplaySettingsW(NULL, Index, &Mode))
            return FALSE;
        if (Mode.dmBitsPerPel == Current->dmBitsPerPel &&
            Mode.dmPelsWidth >= 640 && Mode.dmPelsHeight >= 480 &&
            (Mode.dmPelsWidth != Current->dmPelsWidth || Mode.dmPelsHeight != Current->dmPelsHeight))
        {
            *Other = Mode;
            Other->dmFields = DM_BITSPERPEL | DM_PELSWIDTH | DM_PELSHEIGHT;
            return TRUE;
        }
    }
}

static void
ChangeModeAndBack(void)
{
    DEVMODEW Current, Other;
    LONG Result;

    ok(FindOtherMode(&Current, &Other), "no other display mode\n");
    Result = ChangeDisplaySettingsExW(NULL, &Other, NULL, 0, NULL);
    ok(Result == DISP_CHANGE_SUCCESSFUL, "mode change returned %ld\n", Result);
    Result = ChangeDisplaySettingsExW(NULL, NULL, NULL, 0, NULL);
    ok(Result == DISP_CHANGE_SUCCESSFUL, "mode restore returned %ld\n", Result);
}

static BOOL
RunModeChangeProcess(void)
{
    PROCESS_INFORMATION Process;
    STARTUPINFOA Startup = {sizeof(Startup)};
    char CommandLine[MAX_PATH * 2];
    char **Arguments;
    DWORD ExitCode = 1;
    MSG Message;

    winetest_get_mainargs(&Arguments);
    sprintf(CommandLine, "\"%s\" composition_modechange modechange", Arguments[0]);
    if (!CreateProcessA(NULL, CommandLine, NULL, NULL, FALSE, 0, NULL, NULL, &Startup, &Process))
        return FALSE;
    while (MsgWaitForMultipleObjects(1, &Process.hProcess, FALSE, 30000, QS_ALLINPUT) == WAIT_OBJECT_0 + 1)
    {
        while (PeekMessageW(&Message, NULL, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&Message);
            DispatchMessageW(&Message);
        }
    }
    GetExitCodeProcess(Process.hProcess, &ExitCode);
    CloseHandle(Process.hThread);
    CloseHandle(Process.hProcess);
    return ExitCode == 0;
}

static HRESULT
PresentColor(
    _In_ ID3D11DeviceContext *Context,
    _In_ ID3D11RenderTargetView *View,
    _In_ IDXGISwapChain1 *SwapChain,
    _In_ const float *Color)
{
    RECT Dirty = {0, 0, MODE_VISUAL_WIDTH, MODE_VISUAL_HEIGHT};
    DXGI_PRESENT_PARAMETERS Parameters = {1, &Dirty, NULL, NULL};

    ID3D11DeviceContext_ClearRenderTargetView(Context, View, Color);
    return IDXGISwapChain1_Present1(SwapChain, 0, 0, &Parameters);
}

static void
FillHeldDC(
    _In_ HDC Dc,
    _In_ COLORREF Color)
{
    RECT Rect = {MODE_GDI_X, MODE_GDI_Y, MODE_GDI_X + MODE_GDI_SIZE, MODE_GDI_Y + MODE_GDI_SIZE};
    HBRUSH Brush = CreateSolidBrush(Color);

    FillRect(Dc, &Rect, Brush);
    DeleteObject(Brush);
    GdiFlush();
}

START_TEST(composition_modechange)
{
    static const float Red[4] = {1.0f, 0.0f, 0.0f, 1.0f};
    static const float Green[4] = {0.0f, 1.0f, 0.0f, 1.0f};
    DXGI_SWAP_CHAIN_DESC1 Desc = {0};
    PDCOMPOSITIONCREATEDEVICE CreateComposition;
    PD3D11CREATEDEVICE CreateDevice;
    ID3D11RenderTargetView *View = NULL;
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
    DEVMODEW Before, Other, After;
    WNDCLASSW Class = {0};
    HMODULE D3d11, Dcomp;
    COLORREF Pixel;
    char **Arguments;
    HWND Window, GdiWindow;
    HDC HeldDc;
    HRESULT hr;

    if (winetest_get_mainargs(&Arguments) >= 3 && !strcmp(Arguments[2], "modechange"))
    {
        ChangeModeAndBack();
        return;
    }

    if (!FindOtherMode(&Before, &Other))
    {
        skip("only one display mode is available\n");
        return;
    }

    D3d11 = LoadLibraryW(L"d3d11.dll");
    Dcomp = LoadLibraryW(L"dcomp.dll");
    CreateDevice = D3d11 ? (PD3D11CREATEDEVICE)GetProcAddress(D3d11, "D3D11CreateDevice") : NULL;
    CreateComposition = Dcomp ? (PDCOMPOSITIONCREATEDEVICE)GetProcAddress(Dcomp, "DCompositionCreateDevice") : NULL;
    if (!CreateDevice || !CreateComposition)
    {
        skip("d3d11 or dcomp is unavailable\n");
        return;
    }

    Class.lpfnWndProc = DefWindowProcW;
    Class.hInstance = GetModuleHandleW(NULL);
    Class.hbrBackground = GetStockObject(GRAY_BRUSH);
    Class.lpszClassName = L"composition_modechange";
    RegisterClassW(&Class);
    Window = CreateWindowExW(WS_EX_TOPMOST, Class.lpszClassName, L"composition mode change", WS_POPUP | WS_VISIBLE,
                             100, 100, MODE_WINDOW_WIDTH, MODE_WINDOW_HEIGHT, NULL, NULL, Class.hInstance, NULL);
    ok(Window != NULL, "CreateWindowExW failed, error %lu\n", GetLastError());
    if (!Window)
        return;
    GdiWindow = CreateWindowExW(WS_EX_TOPMOST, Class.lpszClassName, L"held DC mode change", WS_POPUP | WS_VISIBLE,
                                100, 100 + MODE_WINDOW_HEIGHT + 20, MODE_WINDOW_WIDTH, MODE_WINDOW_HEIGHT,
                                NULL, NULL, Class.hInstance, NULL);
    ok(GdiWindow != NULL, "CreateWindowExW failed, error %lu\n", GetLastError());
    SetForegroundWindow(Window);
    UpdateWindow(Window);
    if (GdiWindow)
        UpdateWindow(GdiWindow);
    Pump(200);

    hr = CreateDevice(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, D3D11_CREATE_DEVICE_BGRA_SUPPORT, NULL, 0,
                      D3D11_SDK_VERSION, &Device, NULL, &Context);
    if (FAILED(hr))
    {
        skip("D3D11CreateDevice failed, hr %#lx\n", hr);
        if (GdiWindow)
            DestroyWindow(GdiWindow);
        DestroyWindow(Window);
        return;
    }
    hr = ID3D11Device_QueryInterface(Device, &IID_IDXGIDevice, (void **)&DxgiDevice);
    if (SUCCEEDED(hr))
        hr = IDXGIDevice_GetAdapter(DxgiDevice, &Adapter);
    if (SUCCEEDED(hr))
        hr = IDXGIAdapter_GetParent(Adapter, &IID_IDXGIFactory2, (void **)&Factory);
    ok(hr == S_OK, "IDXGIFactory2 lookup failed, hr %#lx\n", hr);
    if (SUCCEEDED(hr))
    {
        Desc.Width = MODE_VISUAL_WIDTH;
        Desc.Height = MODE_VISUAL_HEIGHT;
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
    if (SUCCEEDED(hr))
        hr = ID3D11Device_CreateRenderTargetView(Device, (ID3D11Resource *)Buffer, NULL, &View);
    if (SUCCEEDED(hr))
        hr = CreateComposition(DxgiDevice, &IID_IDCompositionDevice, (void **)&Composition);
    if (SUCCEEDED(hr))
        hr = IDCompositionDevice_CreateTargetForHwnd(Composition, Window, TRUE, &Target);
    if (SUCCEEDED(hr))
        hr = IDCompositionDevice_CreateVisual(Composition, &Visual);
    if (SUCCEEDED(hr))
        hr = IDCompositionVisual_SetContent(Visual, (IUnknown *)SwapChain);
    if (SUCCEEDED(hr))
        hr = IDCompositionVisual_SetOffsetX(Visual, (float)MODE_VISUAL_X);
    if (SUCCEEDED(hr))
        hr = IDCompositionVisual_SetOffsetY(Visual, (float)MODE_VISUAL_Y);
    if (SUCCEEDED(hr))
        hr = IDCompositionTarget_SetRoot(Target, Visual);
    if (SUCCEEDED(hr))
        hr = IDCompositionDevice_Commit(Composition);
    ok(hr == S_OK, "composition setup failed, hr %#lx\n", hr);
    if (FAILED(hr))
        goto Cleanup;

    HeldDc = GdiWindow ? GetDC(GdiWindow) : NULL;
    ok(HeldDc != NULL, "GetDC failed, error %lu\n", GetLastError());

    hr = PresentColor(Context, View, SwapChain, Red);
    ok(hr == S_OK, "Present1 before the mode change returned %#lx\n", hr);
    IDCompositionDevice_WaitForCommitCompletion(Composition);
    if (HeldDc)
        FillHeldDC(HeldDc, RGB(0, 0, 255));
    Pump(500);
    Pixel = ScreenPixel(Window, MODE_VISUAL_X + MODE_VISUAL_WIDTH / 2, MODE_VISUAL_Y + MODE_VISUAL_HEIGHT / 2);
    ok(MatchesColor(Pixel, 255, 0, 0), "visual pixel before the mode change is %#lx\n", Pixel);
    Pixel = HeldDc ? ScreenPixel(GdiWindow, MODE_GDI_X + MODE_GDI_SIZE / 2, MODE_GDI_Y + MODE_GDI_SIZE / 2) : CLR_INVALID;
    ok(MatchesColor(Pixel, 0, 0, 255), "held DC pixel before the mode change is %#lx\n", Pixel);

    ok(RunModeChangeProcess(), "the mode change process failed\n");
    Pump(500);
    ZeroMemory(&After, sizeof(After));
    After.dmSize = sizeof(After);
    ok(EnumDisplaySettingsW(NULL, ENUM_CURRENT_SETTINGS, &After) &&
       After.dmPelsWidth == Before.dmPelsWidth && After.dmPelsHeight == Before.dmPelsHeight,
       "display mode is %lux%lu, expected %lux%lu\n", After.dmPelsWidth, After.dmPelsHeight,
       Before.dmPelsWidth, Before.dmPelsHeight);

    hr = PresentColor(Context, View, SwapChain, Green);
    ok(hr == S_OK, "Present1 after the mode change returned %#lx\n", hr);
    if (HeldDc)
        FillHeldDC(HeldDc, RGB(0, 255, 0));
    Pump(500);
    Pixel = ScreenPixel(Window, MODE_VISUAL_X + MODE_VISUAL_WIDTH / 2, MODE_VISUAL_Y + MODE_VISUAL_HEIGHT / 2);
    ok(MatchesColor(Pixel, 0, 255, 0), "visual pixel after the mode change is %#lx\n", Pixel);
    Pixel = HeldDc ? ScreenPixel(GdiWindow, MODE_GDI_X + MODE_GDI_SIZE / 2, MODE_GDI_Y + MODE_GDI_SIZE / 2) : CLR_INVALID;
    ok(MatchesColor(Pixel, 0, 255, 0), "held DC pixel after the mode change is %#lx\n", Pixel);

    if (HeldDc)
        ReleaseDC(GdiWindow, HeldDc);

Cleanup:
    if (Visual)
        IDCompositionVisual_Release(Visual);
    if (Target)
        IDCompositionTarget_Release(Target);
    if (Composition)
        IDCompositionDevice_Release(Composition);
    if (View)
        ID3D11RenderTargetView_Release(View);
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
    ID3D11Device_Release(Device);
    if (GdiWindow)
        DestroyWindow(GdiWindow);
    DestroyWindow(Window);
    UnregisterClassW(Class.lpszClassName, Class.hInstance);
}
