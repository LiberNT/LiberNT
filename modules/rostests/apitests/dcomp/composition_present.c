/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests that a composition swap chain bound to a visual reaches the screen
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
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

static BOOL
IsRed(
    _In_ COLORREF Color)
{
    return GetRValue(Color) >= 200 && GetGValue(Color) <= 60 && GetBValue(Color) <= 60;
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

START_TEST(composition_present)
{
    static const float Red[4] = {1.0f, 0.0f, 0.0f, 1.0f};
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
    HMODULE D3d11, Dcomp;
    COLORREF Inside, Outside;
    HWND Window;
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

    Window = CreateWindowExW(WS_EX_TOPMOST, L"static", L"composition present", WS_POPUP | WS_VISIBLE,
                             100, 100, 200, 120, NULL, NULL, NULL, NULL);
    ok(Window != NULL, "CreateWindowExW failed, error %lu\n", GetLastError());
    if (!Window)
        return;
    SetForegroundWindow(Window);
    UpdateWindow(Window);
    Pump(200);

    hr = CreateDevice(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, D3D11_CREATE_DEVICE_BGRA_SUPPORT, NULL, 0,
                      D3D11_SDK_VERSION, &Device, NULL, &Context);
    if (FAILED(hr))
    {
        skip("D3D11CreateDevice failed, hr %#lx\n", hr);
        DestroyWindow(Window);
        return;
    }
    hr = ID3D11Device_QueryInterface(Device, &IID_IDXGIDevice, (void **)&DxgiDevice);
    ok(hr == S_OK, "IDXGIDevice query failed, hr %#lx\n", hr);
    if (SUCCEEDED(hr))
        hr = IDXGIDevice_GetAdapter(DxgiDevice, &Adapter);
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
        hr = IDXGIFactory2_CreateSwapChainForComposition(Factory, (IUnknown *)Device, &Desc, NULL, &SwapChain);
        ok(hr == S_OK, "CreateSwapChainForComposition failed, hr %#lx\n", hr);
    }
    if (SUCCEEDED(hr))
        hr = IDXGISwapChain1_GetBuffer(SwapChain, 0, &IID_ID3D11Texture2D, (void **)&Buffer);
    if (SUCCEEDED(hr))
        hr = ID3D11Device_CreateRenderTargetView(Device, (ID3D11Resource *)Buffer, NULL, &View);
    if (SUCCEEDED(hr))
    {
        ID3D11DeviceContext_ClearRenderTargetView(Context, View, Red);
        hr = IDXGISwapChain1_Present(SwapChain, 0, 0);
        ok(hr == S_OK, "Present failed, hr %#lx\n", hr);
    }

    if (SUCCEEDED(hr))
        hr = CreateComposition(DxgiDevice, &IID_IDCompositionDevice, (void **)&Composition);
    if (SUCCEEDED(hr))
        hr = IDCompositionDevice_CreateTargetForHwnd(Composition, Window, TRUE, &Target);
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
        ID3D11DeviceContext_ClearRenderTargetView(Context, View, Red);
        IDXGISwapChain1_Present(SwapChain, 0, 0);
        IDCompositionDevice_WaitForCommitCompletion(Composition);
        Pump(500);
        Inside = ScreenPixel(Window, VISUAL_X + VISUAL_WIDTH / 2, VISUAL_Y + VISUAL_HEIGHT / 2);
        Outside = ScreenPixel(Window, VISUAL_X + VISUAL_WIDTH + 20, VISUAL_Y + VISUAL_HEIGHT + 20);
        ok(IsRed(Inside), "visual pixel is %#lx\n", Inside);
        ok(!IsRed(Outside), "pixel outside the visual is %#lx\n", Outside);
    }

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
    DestroyWindow(Window);
}
