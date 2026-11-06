/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Composed colour of Direct3D 12 flip swap chains in each flip-model format
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arifing193@gmail.com>
 */

#include "d3d12_apitest.h"

#define FORMAT_WIDTH 96u
#define FORMAT_HEIGHT 64u

static BOOL FormatColorNear(COLORREF pixel, COLORREF expected)
{
    return pixel != CLR_INVALID &&
           abs((int)GetRValue(pixel) - (int)GetRValue(expected)) <= 8 &&
           abs((int)GetGValue(pixel) - (int)GetGValue(expected)) <= 8 &&
           abs((int)GetBValue(pixel) - (int)GetBValue(expected)) <= 8;
}

static COLORREF WaitForFormatColor(HDC screen, POINT origin, COLORREF expected)
{
    DWORD start = GetTickCount();
    COLORREF pixel;

    do
    {
        pixel = GetPixel(screen, origin.x + FORMAT_WIDTH / 2, origin.y + FORMAT_HEIGHT / 2);
        if (FormatColorNear(pixel, expected))
            break;
        Sleep(10);
    } while (GetTickCount() - start < 2000);
    return pixel;
}

static BOOL PresentClear(struct d3d12_test_context *context, IDXGISwapChain3 *chain, const FLOAT *colour)
{
    D3D12_CPU_DESCRIPTOR_HANDLE rtv = d3d12_test_rtv(context, 0);
    ID3D12Resource *buffer = NULL;
    HRESULT hr;

    hr = chain->GetBuffer(chain->GetCurrentBackBufferIndex(), __uuidof(ID3D12Resource),
                          reinterpret_cast<void **>(&buffer));
    ok(hr == S_OK && buffer, "GetBuffer: %#lx\n", hr);
    if (!buffer)
        return FALSE;
    context->device->CreateRenderTargetView(buffer, NULL, rtv);
    hr = context->allocator->Reset();
    ok(hr == S_OK, "allocator Reset: %#lx\n", hr);
    hr = context->list->Reset(context->allocator, NULL);
    ok(hr == S_OK, "list Reset: %#lx\n", hr);
    d3d12_test_transition(context->list, buffer, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
    context->list->ClearRenderTargetView(rtv, colour, 0, NULL);
    d3d12_test_transition(context->list, buffer, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
    buffer->Release();
    if (!d3d12_test_execute(context, context->list, D3D12_TEST_WAIT))
        return FALSE;
    hr = chain->Present(0, 0);
    ok(hr == S_OK, "Present: %#lx\n", hr);
    return hr == S_OK;
}

START_TEST(swapformat)
{
    static const struct
    {
        DXGI_FORMAT format;
        FLOAT colour[4];
        COLORREF expected;
    }
    tests[] =
    {
        {DXGI_FORMAT_B8G8R8A8_UNORM, {0.5f, 0.25f, 1.0f, 1.0f}, RGB(128, 64, 255)},
        {DXGI_FORMAT_R8G8B8A8_UNORM, {1.0f, 0.5f, 0.25f, 1.0f}, RGB(255, 128, 64)},
        {DXGI_FORMAT_R10G10B10A2_UNORM, {0.25f, 1.0f, 0.5f, 1.0f}, RGB(64, 255, 128)},
        {DXGI_FORMAT_R16G16B16A16_FLOAT, {0.5f, 0.25f, 1.0f, 1.0f}, RGB(188, 137, 255)},
    };
    struct d3d12_test_context context;
    IDXGIFactory2 *factory = NULL;
    POINT origin = {360, 320};
    HWND window;
    HDC screen;
    HRESULT hr;

    if (!d3d12_test_init(&context))
    {
        skip("No D3D12 device\n");
        return;
    }
    hr = context.list->Close();
    ok(hr == S_OK, "Close the initial list: %#lx\n", hr);
    hr = CreateDXGIFactory2(0, __uuidof(IDXGIFactory2), reinterpret_cast<void **>(&factory));
    ok(hr == S_OK, "CreateDXGIFactory2: %#lx\n", hr);
    window = CreateWindowExW(WS_EX_TOPMOST | WS_EX_NOACTIVATE, L"STATIC", L"d3d12 swap formats", WS_POPUP | WS_VISIBLE,
                             origin.x, origin.y, FORMAT_WIDTH, FORMAT_HEIGHT, NULL, NULL, NULL, NULL);
    ok(window != NULL, "CreateWindow: %lu\n", GetLastError());
    screen = GetDC(NULL);
    for (UINT i = 0; factory && window && screen && i < ARRAYSIZE(tests); ++i)
    {
        DXGI_SWAP_CHAIN_DESC1 desc = {};
        IDXGISwapChain1 *chain1 = NULL;
        IDXGISwapChain3 *chain = NULL;

        desc.Width = FORMAT_WIDTH;
        desc.Height = FORMAT_HEIGHT;
        desc.Format = tests[i].format;
        desc.SampleDesc.Count = 1;
        desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        desc.BufferCount = 2;
        desc.Scaling = DXGI_SCALING_STRETCH;
        desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        hr = factory->CreateSwapChainForHwnd(context.queue, window, &desc, NULL, NULL, &chain1);
        ok(hr == S_OK && chain1, "format %#x: CreateSwapChainForHwnd: %#lx\n", tests[i].format, hr);
        if (chain1)
            hr = chain1->QueryInterface(__uuidof(IDXGISwapChain3), reinterpret_cast<void **>(&chain));
        if (chain && PresentClear(&context, chain, tests[i].colour))
        {
            COLORREF pixel = WaitForFormatColor(screen, origin, tests[i].expected);
            ok(FormatColorNear(pixel, tests[i].expected), "format %#x: screen colour %06lx, expected %06lx\n",
               tests[i].format, pixel, tests[i].expected);
        }
        d3d12_test_wait_queue(context.device, context.queue, D3D12_TEST_WAIT);
        if (chain) chain->Release();
        if (chain1) chain1->Release();
    }
    if (screen) ReleaseDC(NULL, screen);
    if (window) DestroyWindow(window);
    if (factory) factory->Release();
    d3d12_test_destroy(&context);
}
