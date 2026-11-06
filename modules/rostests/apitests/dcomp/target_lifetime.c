/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests that a composition swap chain keeps presenting after its visual's target window is gone
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arifing193@gmail.com>
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

#define VISUAL_X 40
#define VISUAL_Y 30
#define VISUAL_WIDTH 64
#define VISUAL_HEIGHT 32

typedef struct _CHAIN_TEST
{
    ID3D11Device *Device;
    ID3D11DeviceContext *Context;
    IDXGIDevice *DxgiDevice;
    IDXGISwapChain1 *SwapChain;
    ID3D11Texture2D *Buffer;
    ID3D11RenderTargetView *View;
    IDCompositionDevice *Composition;
} CHAIN_TEST;

static BOOL
IsColor(
    _In_ COLORREF Color,
    _In_ BOOL Red,
    _In_ BOOL Green)
{
    BOOL HasRed = GetRValue(Color) >= 200, HasGreen = GetGValue(Color) >= 200;

    return HasRed == Red && HasGreen == Green && GetBValue(Color) <= 60 &&
           (Red || GetRValue(Color) <= 60) && (Green || GetGValue(Color) <= 60);
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

static HWND
CreateTargetWindow(
    _In_ int X,
    _In_ int Y)
{
    HWND Window;

    Window = CreateWindowExW(WS_EX_TOPMOST, L"static", L"composition target", WS_POPUP | WS_VISIBLE,
                             X, Y, 200, 120, NULL, NULL, NULL, NULL);
    ok(Window != NULL, "CreateWindowExW failed, error %lu\n", GetLastError());
    if (!Window)
        return NULL;
    SetForegroundWindow(Window);
    UpdateWindow(Window);
    Pump(200);
    return Window;
}

static HRESULT
CreateChain(
    _Inout_ CHAIN_TEST *Test,
    _In_ PD3D11CREATEDEVICE CreateDevice,
    _In_ PDCOMPOSITIONCREATEDEVICE CreateComposition)
{
    DXGI_SWAP_CHAIN_DESC1 Desc = {0};
    IDXGIFactory2 *Factory = NULL;
    IDXGIAdapter *Adapter = NULL;
    HRESULT hr;

    hr = CreateDevice(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, D3D11_CREATE_DEVICE_BGRA_SUPPORT, NULL, 0,
                      D3D11_SDK_VERSION, &Test->Device, NULL, &Test->Context);
    if (FAILED(hr))
        return hr;
    hr = ID3D11Device_QueryInterface(Test->Device, &IID_IDXGIDevice, (void **)&Test->DxgiDevice);
    if (SUCCEEDED(hr))
        hr = IDXGIDevice_GetAdapter(Test->DxgiDevice, &Adapter);
    if (SUCCEEDED(hr))
        hr = IDXGIAdapter_GetParent(Adapter, &IID_IDXGIFactory2, (void **)&Factory);
    ok(hr == S_OK, "IDXGIFactory2 lookup failed, hr %#lx\n", hr);
    if (SUCCEEDED(hr))
    {
        Desc.Width = VISUAL_WIDTH;
        Desc.Height = VISUAL_HEIGHT;
        Desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        Desc.SampleDesc.Count = 1;
        Desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        Desc.BufferCount = 2;
        Desc.Scaling = DXGI_SCALING_STRETCH;
        Desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
        Desc.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;
        hr = IDXGIFactory2_CreateSwapChainForComposition(Factory, (IUnknown *)Test->Device, &Desc, NULL,
                                                         &Test->SwapChain);
        ok(hr == S_OK, "CreateSwapChainForComposition failed, hr %#lx\n", hr);
    }
    if (SUCCEEDED(hr))
        hr = IDXGISwapChain1_GetBuffer(Test->SwapChain, 0, &IID_ID3D11Texture2D, (void **)&Test->Buffer);
    if (SUCCEEDED(hr))
        hr = ID3D11Device_CreateRenderTargetView(Test->Device, (ID3D11Resource *)Test->Buffer, NULL, &Test->View);
    if (SUCCEEDED(hr))
        hr = CreateComposition(Test->DxgiDevice, &IID_IDCompositionDevice, (void **)&Test->Composition);
    ok(hr == S_OK, "chain setup failed, hr %#lx\n", hr);
    if (Factory)
        IDXGIFactory2_Release(Factory);
    if (Adapter)
        IDXGIAdapter_Release(Adapter);
    return hr;
}

static void
DestroyChain(
    _Inout_ CHAIN_TEST *Test)
{
    if (Test->Composition)
        IDCompositionDevice_Release(Test->Composition);
    if (Test->View)
        ID3D11RenderTargetView_Release(Test->View);
    if (Test->Buffer)
        ID3D11Texture2D_Release(Test->Buffer);
    if (Test->SwapChain)
        IDXGISwapChain1_Release(Test->SwapChain);
    if (Test->DxgiDevice)
        IDXGIDevice_Release(Test->DxgiDevice);
    if (Test->Context)
        ID3D11DeviceContext_Release(Test->Context);
    if (Test->Device)
        ID3D11Device_Release(Test->Device);
}

static HRESULT
BindChain(
    _In_ CHAIN_TEST *Test,
    _In_ HWND Window,
    _Out_ IDCompositionTarget **Target,
    _Out_ IDCompositionVisual **Visual)
{
    HRESULT hr;

    *Target = NULL;
    *Visual = NULL;
    hr = IDCompositionDevice_CreateTargetForHwnd(Test->Composition, Window, TRUE, Target);
    if (SUCCEEDED(hr))
        hr = IDCompositionDevice_CreateVisual(Test->Composition, Visual);
    if (SUCCEEDED(hr))
        hr = IDCompositionVisual_SetContent(*Visual, (IUnknown *)Test->SwapChain);
    if (SUCCEEDED(hr))
        hr = IDCompositionVisual_SetOffsetX(*Visual, (float)VISUAL_X);
    if (SUCCEEDED(hr))
        hr = IDCompositionVisual_SetOffsetY(*Visual, (float)VISUAL_Y);
    if (SUCCEEDED(hr))
        hr = IDCompositionTarget_SetRoot(*Target, *Visual);
    if (SUCCEEDED(hr))
        hr = IDCompositionDevice_Commit(Test->Composition);
    if (SUCCEEDED(hr))
        hr = IDCompositionDevice_WaitForCommitCompletion(Test->Composition);
    return hr;
}

static void
ReleaseBinding(
    _In_opt_ IDCompositionTarget *Target,
    _In_opt_ IDCompositionVisual *Visual)
{
    if (Visual)
        IDCompositionVisual_Release(Visual);
    if (Target)
        IDCompositionTarget_Release(Target);
}

static HRESULT
PresentColor(
    _In_ CHAIN_TEST *Test,
    _In_ const float *Color,
    _In_opt_ const RECT *Dirty)
{
    DXGI_PRESENT_PARAMETERS Parameters = {0};
    RECT Rect;

    ID3D11DeviceContext_ClearRenderTargetView(Test->Context, Test->View, Color);
    if (!Dirty)
        return IDXGISwapChain1_Present(Test->SwapChain, 0, 0);
    Rect = *Dirty;
    Parameters.DirtyRectsCount = 1;
    Parameters.pDirtyRects = &Rect;
    return IDXGISwapChain1_Present1(Test->SwapChain, 0, 0, &Parameters);
}

static void
TestDestroyedTarget(
    _In_ CHAIN_TEST *Test)
{
    static const float Red[4] = {1.0f, 0.0f, 0.0f, 1.0f};
    static const float Green[4] = {0.0f, 1.0f, 0.0f, 1.0f};
    static const RECT Full = {0, 0, VISUAL_WIDTH, VISUAL_HEIGHT};
    static const RECT Corner = {0, 0, 16, 16};
    IDCompositionTarget *Target;
    IDCompositionVisual *Visual;
    COLORREF Pixel;
    HWND First, Second;
    HRESULT hr;

    First = CreateTargetWindow(100, 100);
    if (!First)
        return;
    hr = PresentColor(Test, Red, NULL);
    ok(hr == S_OK, "unbound Present failed, hr %#lx\n", hr);
    hr = BindChain(Test, First, &Target, &Visual);
    ok(hr == S_OK, "binding to the first window failed, hr %#lx\n", hr);
    hr = PresentColor(Test, Red, NULL);
    ok(hr == S_OK, "bound Present failed, hr %#lx\n", hr);
    Pump(500);
    Pixel = ScreenPixel(First, VISUAL_X + VISUAL_WIDTH / 2, VISUAL_Y + VISUAL_HEIGHT / 2);
    ok(IsColor(Pixel, TRUE, FALSE), "bound visual pixel is %#lx\n", Pixel);

    DestroyWindow(First);
    Pump(300);
    hr = PresentColor(Test, Green, NULL);
    ok(hr == S_OK, "Present after the target window was destroyed returned %#lx\n", hr);
    hr = PresentColor(Test, Green, &Full);
    ok(hr == S_OK, "Present1 with a full dirty rect after the target window was destroyed returned %#lx\n", hr);
    hr = PresentColor(Test, Green, &Corner);
    ok(hr == S_OK, "Present1 with a partial dirty rect after the target window was destroyed returned %#lx\n", hr);
    ReleaseBinding(Target, Visual);

    Second = CreateTargetWindow(400, 100);
    if (!Second)
        return;
    hr = BindChain(Test, Second, &Target, &Visual);
    ok(hr == S_OK, "binding to the second window failed, hr %#lx\n", hr);
    Pump(500);
    Pixel = ScreenPixel(Second, VISUAL_X + VISUAL_WIDTH / 2, VISUAL_Y + VISUAL_HEIGHT / 2);
    ok(IsColor(Pixel, FALSE, TRUE), "rebound visual shows %#lx instead of the frame presented while unbound\n", Pixel);

    ShowWindow(Second, SW_MINIMIZE);
    Pump(300);
    hr = PresentColor(Test, Red, NULL);
    ok(hr == S_OK, "Present with a minimized target window returned %#lx\n", hr);
    ReleaseBinding(Target, Visual);
    DestroyWindow(Second);
}

START_TEST(target_lifetime)
{
    PDCOMPOSITIONCREATEDEVICE CreateComposition;
    PD3D11CREATEDEVICE CreateDevice;
    CHAIN_TEST Test = {0};
    HMODULE D3d11, Dcomp;
    HRESULT hr;

    D3d11 = LoadLibraryW(L"d3d11.dll");
    Dcomp = LoadLibraryW(L"dcomp.dll");
    CreateDevice = D3d11 ? (PD3D11CREATEDEVICE)GetProcAddress(D3d11, "D3D11CreateDevice") : NULL;
    CreateComposition = Dcomp ? (PDCOMPOSITIONCREATEDEVICE)GetProcAddress(Dcomp, "DCompositionCreateDevice") : NULL;
    if (!CreateDevice || !CreateComposition)
    {
        skip("d3d11 or dcomp is unavailable\n");
        return;
    }
    hr = CreateChain(&Test, CreateDevice, CreateComposition);
    if (SUCCEEDED(hr))
        TestDestroyedTarget(&Test);
    else
        skip("composition chain setup failed, hr %#lx\n", hr);
    DestroyChain(&Test);
}
