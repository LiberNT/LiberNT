/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test the source region of composition swap chains
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <apitest.h>
#include <initguid.h>
#include <d3d11.h>
#include <dxgi1_4.h>
#include <dcomp.h>

#define WIDTH 128
#define HEIGHT 96
#define SOURCE_WIDTH 64
#define SOURCE_HEIGHT 48
#define INSIDE_PIXEL 0xffff0000
#define OUTSIDE_PIXEL 0xff0000ff

static const COLORREF Inside = RGB(255, 0, 0);
static const COLORREF Outside = RGB(0, 0, 255);
static const POINT Cropped[] = {{96, 72}, {96, 24}, {32, 72}};

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
CheckSourceSize(IDXGISwapChain2 *Chain, UINT Width, UINT Height, const char *What)
{
    UINT ActualWidth = ~0u, ActualHeight = ~0u;
    HRESULT Hr = Chain->GetSourceSize(&ActualWidth, &ActualHeight);

    ok(Hr == S_OK && ActualWidth == Width && ActualHeight == Height,
       "%s: GetSourceSize %#lx %u x %u, expected %u x %u\n", What, Hr, ActualWidth, ActualHeight, Width, Height);
}

static void
TestSourceSizeContract(IDXGISwapChain2 *Chain)
{
    CheckSourceSize(Chain, WIDTH, HEIGHT, "Initial");
    ok(Chain->SetSourceSize(WIDTH + 1, HEIGHT) == DXGI_ERROR_INVALID_CALL, "Wider than the buffers accepted\n");
    ok(Chain->SetSourceSize(WIDTH, HEIGHT + 1) == DXGI_ERROR_INVALID_CALL, "Taller than the buffers accepted\n");
    CheckSourceSize(Chain, WIDTH, HEIGHT, "After oversize");
    ok(Chain->SetSourceSize(0, HEIGHT) == S_OK, "Zero width refused\n");
    CheckSourceSize(Chain, 0, HEIGHT, "Zero width");
    ok(Chain->SetSourceSize(WIDTH, 0) == S_OK, "Zero height refused\n");
    CheckSourceSize(Chain, WIDTH, 0, "Zero height");
    ok(Chain->SetSourceSize(0, 0) == S_OK, "Empty source refused\n");
    CheckSourceSize(Chain, 0, 0, "Empty source");
    ok(Chain->SetSourceSize(SOURCE_WIDTH, SOURCE_HEIGHT) == S_OK, "Partial source refused\n");
    CheckSourceSize(Chain, SOURCE_WIDTH, SOURCE_HEIGHT, "Partial source");
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

static void
TestSourceRegionOnScreen(ID3D11DeviceContext *Context, IDXGISwapChain2 *Chain, HDC Screen, POINT Origin)
{
    COLORREF Actual;

    if (!PresentFrame(Context, Chain))
        return;
    if (!WaitForScreenColor(Screen, Origin, SOURCE_WIDTH / 2, SOURCE_HEIGHT / 2, Inside))
    {
        ok(0, "The source region never reached the screen\n");
        return;
    }
    for (UINT i = 0; i < ARRAYSIZE(Cropped); ++i)
    {
        BOOL Shown = ScreenColorNear(Screen, Origin, Cropped[i].x, Cropped[i].y, Outside, &Actual);
        ok(!Shown && Actual != CLR_INVALID, "Pixel %ld,%ld outside the source region shows %06lx\n",
           Cropped[i].x, Cropped[i].y, Actual);
    }

    ok(Chain->SetSourceSize(WIDTH, HEIGHT) == S_OK, "Full source refused\n");
    if (!PresentFrame(Context, Chain))
        return;
    for (UINT i = 0; i < ARRAYSIZE(Cropped); ++i)
        ok(WaitForScreenColor(Screen, Origin, Cropped[i].x, Cropped[i].y, Outside),
           "Pixel %ld,%ld of the full source never reached the screen\n", Cropped[i].x, Cropped[i].y);
}

START_TEST(composition_source)
{
    typedef HRESULT (WINAPI *CREATE_DEVICE)(IDXGIAdapter *, D3D_DRIVER_TYPE, HMODULE,
        UINT, const D3D_FEATURE_LEVEL *, UINT, UINT, ID3D11Device **,
        D3D_FEATURE_LEVEL *, ID3D11DeviceContext **);
    typedef HRESULT (WINAPI *CREATE_COMPOSITION)(IDXGIDevice *, REFIID, void **);
    HMODULE Runtime = LoadLibraryW(L"d3d11.dll"), Dcomp = LoadLibraryW(L"dcomp.dll");
    if (!Runtime || !Dcomp) { skip("D3D11/DirectComposition unavailable\n"); return; }
    CREATE_DEVICE Create = reinterpret_cast<CREATE_DEVICE>(GetProcAddress(Runtime, "D3D11CreateDevice"));
    CREATE_COMPOSITION CreateComposition = reinterpret_cast<CREATE_COMPOSITION>(GetProcAddress(Dcomp, "DCompositionCreateDevice"));
    if (!Create || !CreateComposition) { skip("Creation exports unavailable\n"); return; }

    ID3D11Device *Device = NULL;
    ID3D11DeviceContext *Context = NULL;
    HRESULT Hr = Create(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
                        NULL, 0, D3D11_SDK_VERSION, &Device, NULL, &Context);
    if (FAILED(Hr)) { skip("Hardware D3D11 unavailable: %#lx\n", Hr); return; }

    IDXGIDevice *Dxgi = NULL;
    IDXGIAdapter *Adapter = NULL;
    IDXGIFactory2 *Factory = NULL;
    Hr = Device->QueryInterface(IID_IDXGIDevice, reinterpret_cast<void **>(&Dxgi));
    ok(Hr == S_OK, "IDXGIDevice: %#lx\n", Hr);
    if (Dxgi) Hr = Dxgi->GetAdapter(&Adapter);
    if (Adapter) Hr = Adapter->GetParent(IID_IDXGIFactory2, reinterpret_cast<void **>(&Factory));
    ok(Hr == S_OK && Factory, "IDXGIFactory2: %#lx\n", Hr);

    POINT Origin = {260, 220};
    HWND Window = CreateWindowExW(WS_EX_TOPMOST | WS_EX_NOACTIVATE, L"STATIC", L"composition source",
                                  WS_POPUP | WS_VISIBLE, Origin.x, Origin.y, WIDTH, HEIGHT,
                                  NULL, NULL, NULL, NULL);
    ok(Window != NULL, "CreateWindow: %lu\n", GetLastError());
    HDC Screen = GetDC(NULL);

    IDXGISwapChain1 *Chain1 = NULL;
    IDXGISwapChain2 *Chain = NULL;
    IDCompositionDevice *Composition = NULL;
    IDCompositionTarget *Target = NULL;
    IDCompositionVisual *Visual = NULL;
    if (Factory && Window && Screen)
    {
        UpdateWindow(Window);
        DXGI_SWAP_CHAIN_DESC1 Desc = {};
        Desc.Width = WIDTH;
        Desc.Height = HEIGHT;
        Desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        Desc.SampleDesc.Count = 1;
        Desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        Desc.BufferCount = 2;
        Desc.Scaling = DXGI_SCALING_STRETCH;
        Desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
        Desc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
        Hr = Factory->CreateSwapChainForComposition(Device, &Desc, NULL, &Chain1);
        ok(Hr == S_OK && Chain1, "CreateSwapChainForComposition: %#lx\n", Hr);
        if (Chain1) Hr = Chain1->QueryInterface(IID_IDXGISwapChain2, reinterpret_cast<void **>(&Chain));
        ok(Hr == S_OK && Chain, "IDXGISwapChain2: %#lx\n", Hr);
        Hr = CreateComposition(Dxgi, IID_IDCompositionDevice, reinterpret_cast<void **>(&Composition));
        ok(Hr == S_OK && Composition, "DCompositionCreateDevice: %#lx\n", Hr);
        if (Composition) Hr = Composition->CreateTargetForHwnd(Window, TRUE, &Target);
        ok(Hr == S_OK && Target, "CreateTargetForHwnd: %#lx\n", Hr);
        if (Composition) Hr = Composition->CreateVisual(&Visual);
        ok(Hr == S_OK && Visual, "CreateVisual: %#lx\n", Hr);
    }
    if (Chain && Visual && Target)
    {
        TestSourceSizeContract(Chain);
        Hr = Target->SetRoot(Visual);
        ok(Hr == S_OK, "SetRoot: %#lx\n", Hr);
        Hr = Visual->SetContent(Chain);
        ok(Hr == S_OK, "SetContent: %#lx\n", Hr);
        Hr = Composition->Commit();
        ok(Hr == S_OK, "Commit: %#lx\n", Hr);
        TestSourceRegionOnScreen(Context, Chain, Screen, Origin);
        Visual->SetContent(NULL);
        Target->SetRoot(NULL);
        Composition->Commit();

        ok(Chain->SetSourceSize(SOURCE_WIDTH, SOURCE_HEIGHT) == S_OK, "Partial source refused\n");
        Hr = Chain->ResizeBuffers(0, WIDTH, HEIGHT, DXGI_FORMAT_UNKNOWN, 0);
        ok(Hr == S_OK, "ResizeBuffers: %#lx\n", Hr);
        CheckSourceSize(Chain, WIDTH, HEIGHT, "After ResizeBuffers");
    }

    if (Visual) Visual->Release();
    if (Target) Target->Release();
    if (Composition) Composition->Release();
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
    FreeLibrary(Dcomp);
    FreeLibrary(Runtime);
}
